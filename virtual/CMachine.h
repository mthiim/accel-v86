#pragma once

#include <windows.h>
#include <WinHvEmulation.h>
#include <WinHvPlatform.h>

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "include/cef_app.h"
#include "include/cef_base.h"
#include "include/base/cef_lock.h"


class semaphore
{
private:
	std::mutex mutex_;
	std::condition_variable condition_;
	unsigned long count_ = 0; // Initialized as locked.

public:
	void notify() {
		std::lock_guard<decltype(mutex_)> lock(mutex_);
		count_ = 1;
		condition_.notify_one();
	}

	void wait() {
		std::unique_lock<decltype(mutex_)> lock(mutex_);
		while (!count_) // Handle spurious wake-ups.
			condition_.wait(lock);
		count_ = 0;
	}
};

void StopperFunction(HANDLE partitionHandle, semaphore* sem);


HRESULT GetVirtualRegisters(VOID* Context,
	const WHV_REGISTER_NAME* RegisterNames,
	unsigned int RegisterCount,
	WHV_REGISTER_VALUE* RegisterValues);

HRESULT IoPortCallback(VOID* Context, WHV_EMULATOR_IO_ACCESS_INFO* IoAccess);

HRESULT SetVirtualRegisters(VOID* Context,
	const WHV_REGISTER_NAME* RegisterNames,
	UINT32 RegisterCount,
	const WHV_REGISTER_VALUE* RegisterValues);

HRESULT MemoryCallback(VOID* Context,
	WHV_EMULATOR_MEMORY_ACCESS_INFO* MemoryAccess);

HRESULT TranslateRange(
	VOID* Context,
	WHV_GUEST_VIRTUAL_ADDRESS GvaPage,
	WHV_TRANSLATE_GVA_FLAGS TranslateFlags,
	WHV_TRANSLATE_GVA_RESULT_CODE* TranslationResult,
	WHV_GUEST_PHYSICAL_ADDRESS*
	GpaPage  // NOTE: This pointer _must_ be 4K page aligned
);

class CMachine : public CefBaseRefCounted {
private:
	std::unique_ptr<unsigned char[]> pUnalignedMemory;
	std::unique_ptr<unsigned char[]> pUnalignedParamBuffer;


	unsigned char* pMemory;

	size_t m_sz;
	WHV_EMULATOR_HANDLE emulatorHandle;
	HANDLE partitionHandle;
	CefRefPtr<CefV8Value> jsobj;
	std::thread stopperThread;

	int entry_counter = 0;
	int run_loop_counter = 0;
	int io_counter = 0;
	int irq_counter = 0;
	int mem_counter = 0;
	int inthandle_counter = 0;

	semaphore sem;

	CefRefPtr<CefV8Value> mw1, mw2, mw4, mr1, mr2, mr4, jscpu;
	unsigned int* parambuf;


public:
	virtual ~CMachine()
	{
		// TODO
		MessageBox(NULL, L"Destructing", L"Destruction", MB_OK);
	}
	CMachine(size_t sz, CefRefPtr<CefV8Value> cpu, CefRefPtr<CefV8Value> mw1, CefRefPtr<CefV8Value> mw2, CefRefPtr<CefV8Value> mw4, CefRefPtr<CefV8Value> mr1, CefRefPtr<CefV8Value> mr2, CefRefPtr<CefV8Value> mr4)
		: mr1(mr1), mr2(mr2), mr4(mr4), mw1(mw1), mw2(mw2), mw4(mw4), jscpu(cpu)
	{
		// Initialize the instruction emulator and callbacks
		WHV_EMULATOR_CALLBACKS callbacks;
		memset(&callbacks, 0x0, sizeof(callbacks));
		callbacks.Size = sizeof(callbacks);
		callbacks.WHvEmulatorGetVirtualProcessorRegisters = &GetVirtualRegisters;
		callbacks.WHvEmulatorIoPortCallback = &IoPortCallback;
		callbacks.WHvEmulatorMemoryCallback = &MemoryCallback;
		callbacks.WHvEmulatorSetVirtualProcessorRegisters = &SetVirtualRegisters;
		callbacks.WHvEmulatorTranslateGvaPage = &TranslateRange;

		HRESULT hr = WHvEmulatorCreateEmulator(&callbacks, &emulatorHandle);
		if (hr != S_OK) {
			throw std::exception("Couldn't create emulator!");
		}

		hr = WHvCreatePartition(&partitionHandle);
		if (hr != S_OK) {
			throw std::exception("Couldn't create partition!");
		}
		stopperThread = std::thread(&StopperFunction, partitionHandle, &sem);


		DWORD procCnt = 1;
		hr = WHvSetPartitionProperty(partitionHandle, WHvPartitionPropertyCodeProcessorCount,
			&procCnt, sizeof(procCnt));
		if (hr != S_OK) {
			throw std::exception("Couldn't set property count");
		}

		WHV_X64_LOCAL_APIC_EMULATION_MODE mode = WHvX64LocalApicEmulationModeNone;
		hr = WHvSetPartitionProperty(partitionHandle, WHvPartitionPropertyCodeLocalApicEmulationMode,
			&mode, sizeof(mode));
		if (hr != S_OK) {
			throw std::exception("Couldn't set property count");
		}

		UINT32 exitList[19];
		int exitListCnt = 0;
		exitList[exitListCnt++] = 18;
		for (unsigned int i = 0; i <= 8; i++) {
			exitList[exitListCnt++] = i;
			exitList[exitListCnt++] = 0x80000000 + i;
		}

		hr = WHvSetPartitionProperty(partitionHandle, WHvPartitionPropertyCodeCpuidExitList,
			exitList, exitListCnt * sizeof(UINT32));
		if (hr != S_OK) {
			throw std::exception("Couldn't set CPUID exit list");
		}

		WHV_PARTITION_PROPERTY prop;
		memset(&prop, 0, sizeof(prop));
		prop.ExtendedVmExits.X64MsrExit = 1;
		prop.ExtendedVmExits.X64CpuidExit = 1;
		hr = WHvSetPartitionProperty(
			partitionHandle,
			WHvPartitionPropertyCodeExtendedVmExits,
			&prop,
			sizeof(WHV_PARTITION_PROPERTY));

		if (hr != S_OK) {
			throw std::exception("Couldn't set CPUID exit list");
		}

		hr = WHvSetupPartition(partitionHandle);
		if (hr != S_OK) {
			throw std::exception("Couldn't setup partition!");
		}

		pUnalignedParamBuffer = std::make_unique<unsigned char[]>(8192);
		parambuf = (unsigned int*)(((unsigned long long)pUnalignedParamBuffer.get() + 4096) & 0xFFFFFFFFFFFFF000);

		m_sz = sz;
		pUnalignedMemory = std::make_unique<unsigned char[]>(sz + 4096);
		pMemory = (unsigned char*)(((unsigned long long)pUnalignedMemory.get() + 4096) & 0xFFFFFFFFFFFFF000);

		hr = memmap(pMemory, sz, 1, 0);  // A20 gate on per default for now (TODO)
		if (hr != S_OK) {
			throw std::exception("Couldn't map memory!");
		}

		hr = WHvCreateVirtualProcessor(partitionHandle, 0, 0);
		if (hr != S_OK) {
			throw std::exception("Couldn't create virtual proc!");
		}

		DWORD biossize = 131072;
		DWORD biosOffset = 0x100000 - biossize;
		unsigned int topBios = (unsigned int)(-((int)biossize));

		// Create BIOS shadow mapping
		hr = WHvMapGpaRange(partitionHandle, pMemory + biosOffset, topBios, 0x100000,
			WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite |
			WHvMapGpaRangeFlagExecute);
		if (hr != S_OK) {
			throw std::exception("Error, couldn't map BIOS!");
		}

		WHV_REGISTER_NAME names[13] = {
			WHvX64RegisterCr0,    WHvX64RegisterRip,  WHvX64RegisterCs,
			WHvX64RegisterRflags, WHvX64RegisterDs,   WHvX64RegisterEs,
			WHvX64RegisterFs,     WHvX64RegisterGs,   WHvX64RegisterRdx,
			WHvX64RegisterGdtr,   WHvX64RegisterLdtr, WHvX64RegisterIdtr,
			WHvX64RegisterTr };
		WHV_REGISTER_VALUE oldvalues[13];
		hr = WHvGetVirtualProcessorRegisters(partitionHandle, 0, names, 13, oldvalues);
		if (hr != S_OK) {
			throw std::exception("Error, couldn't load BIOS!");
		}

		for (int i = 0; i < 13; i++) {
			printf("Old value for %d = %llx\n", i, oldvalues[i].Reg64);
		}

		WHV_REGISTER_VALUE values[15];
		int ss = sizeof(values);
		memset(&values[0], 0x0, ss);

		values[0].Reg32 = 0x60000010;
		values[1].Reg64 = 0xFFF0LL;
		values[3].Reg64 = 2;

		WHV_X64_SEGMENT_REGISTER reg;
		memset(&reg, 0x0, sizeof(reg));
		reg.Limit = 0xFFFF;
		reg.Attributes = 0x93;
		values[4].Segment = reg;  // DS
		values[5].Segment = reg;  // ES
		values[6].Segment = reg;  // FS
		values[7].Segment = reg;  // GS

		reg.Selector = 0xF000;
		reg.Base = 0xFFFF0000;

		values[2].Segment = reg;  // CS

		values[8].Reg64 = 0;

		values[9].Segment.Limit = 0xFFFF;  // GDT

		values[10].Segment.Attributes = 0x82;  // LDT

		values[11].Segment.Limit = 0xFFFF;  // IDT

		values[12].Segment = values[10].Segment;  // TR (same settings as LDT)
		values[12].Segment.Attributes = 0x83;

		values[9].Segment = oldvalues[9].Segment;
		values[11].Segment = oldvalues[11].Segment;


		// Note it is deliberately we don't include the last one (Apic ID) because we don't want to write to it!
		printf("Difference between originals and what we are setting...\n");
		for (int i = 0; i < 13; i++) {
			if (values[i].Reg64 != oldvalues[i].Reg64) {
				printf("New value for %d %llx %llx\n", i, values[i].Reg64,
					oldvalues[i].Reg64);
			}
		}

		hr = WHvSetVirtualProcessorRegisters(partitionHandle, 0, names, 13, values);
		if (hr != S_OK) {
			throw std::exception("Error, couldn't set virtual registers!");
		}
	}

	CefRefPtr<CefV8Value> run() {
		entry_counter++;

		std::chrono::time_point<std::chrono::system_clock> now =
			std::chrono::system_clock::now();

		auto duration = now.time_since_epoch();
		auto millis_start = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

		unsigned int halted = 0; // Indicates if machine is halted 
		unsigned int dontbreak = 0; // Indicates if we can't break out now even if we reach the time slice (need to finish interrupt delivery)

		while (true) {
			run_loop_counter++;

			now =
				std::chrono::system_clock::now();

			if (dontbreak == 0) {
				duration = now.time_since_epoch();
				auto millis_end = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
				auto diff = (millis_end - millis_start);
				if (diff > 2) {
					break;
				}
			}
			else {
				dontbreak = 0;
			}




			WHV_RUN_VP_EXIT_CONTEXT ctx;
			memset(&ctx, 0x0, sizeof(ctx));

			sem.notify();
			HRESULT hr = WHvRunVirtualProcessor(partitionHandle, 0x0, &ctx, sizeof(ctx));
			if (hr != S_OK) {
				throw std::exception("Error running virtual processor");
			}




			if (ctx.ExitReason == WHvRunVpExitReasonX64IoPortAccess) {
				WHV_EMULATOR_STATUS status;
				hr = WHvEmulatorTryIoEmulation(emulatorHandle, (VOID*)this, &ctx.VpContext,
					&ctx.IoPortAccess, &status);
				if (hr != S_OK) {
					throw std::exception("I/O emulation gave error");
				}
				if (!status.EmulationSuccessful) {
					throw std::exception("I/O emulation not successful");
				}
			}
			else if (ctx.ExitReason == WHvRunVpExitReasonMemoryAccess) {
				WHV_EMULATOR_STATUS status;
				hr = WHvEmulatorTryMmioEmulation(emulatorHandle, (VOID*)this, &ctx.VpContext, &ctx.MemoryAccess, &status);
				if (hr != S_OK) {
					throw std::exception("MMIO emulation gave error");
				}
				if (!status.EmulationSuccessful) {
					throw std::exception("MMIO emulation not successful");
				}

			}
			else if (ctx.ExitReason == WHvRunVpExitReasonX64Cpuid) {
				// Simulation of CPUID by passing to the JS side

				CefV8ValueList list;
				list.push_back(CefV8Value::CreateUInt(ctx.CpuidAccess.Rax));
				list.push_back(CefV8Value::CreateUInt(ctx.CpuidAccess.Rbx));
				list.push_back(CefV8Value::CreateUInt(ctx.CpuidAccess.Rcx));
				list.push_back(CefV8Value::CreateUInt(ctx.CpuidAccess.Rdx));
				CefRefPtr<CefV8Value> cb = jsobj->GetValue("cpuid");
				CefRefPtr<CefV8Value> retval = cb->ExecuteFunction(jsobj, list);

				CefRefPtr<CefV8Value> retval1 = retval->GetValue(0);
				CefRefPtr<CefV8Value> retval2 = retval->GetValue(1);
				CefRefPtr<CefV8Value> retval3 = retval->GetValue(2);
				CefRefPtr<CefV8Value> retval4 = retval->GetValue(3);

				WHV_REGISTER_VALUE values[5];
				values[0].Reg64 = retval1->GetUIntValue();
				values[1].Reg64 = retval2->GetUIntValue();
				values[2].Reg64 = retval3->GetUIntValue();
				values[3].Reg64 = retval4->GetUIntValue();

				UINT64 rip = ctx.VpContext.Rip;
				rip += ctx.VpContext.InstructionLength;

				values[4].Reg64 = rip;
				WHV_REGISTER_NAME names[5] = { WHvX64RegisterRax, WHvX64RegisterRbx, WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterRip };


				hr = WHvSetVirtualProcessorRegisters(partitionHandle, 0x0, names, 5, values);
				if (hr != S_OK) {
					throw std::exception("Error setting virtual registers");
				}
			}
			else if (ctx.ExitReason == WHvRunVpExitReasonX64InterruptWindow) {
				dontbreak = 1;
				continue;
			}
			else if (ctx.ExitReason == WHvRunVpExitReasonCanceled) {
				continue;
			}
			else if (ctx.ExitReason == WHvRunVpExitReasonX64Halt) {
				halted = 1;
				break;
			}
			else {
				throw std::exception("Unknown exit reaosn");
				/*
				WHV_REGISTER_NAME nn[4] = {
				WHvX64RegisterRip, WHvRegisterPendingInterruption, WHvX64RegisterDeliverabilityNotifications, WHvX64RegisterRflags };
				WHV_REGISTER_VALUE vv[4];

				WHvGetVirtualProcessorRegisters(partitionHandle, 0, nn, 4, vv);
				MessageBox(NULL, L"Error - uknown exit reasoin", L"Error", MB_OK); */

				//throw std::exception("Unknown reason 2"); 
			}
		}

		// OK, about to exit - update the state of the JS side
		WHV_REGISTER_NAME nn[4] = {
		WHvX64RegisterRip, WHvRegisterPendingInterruption, WHvX64RegisterDeliverabilityNotifications, WHvX64RegisterRflags };
		WHV_REGISTER_VALUE vv[4];

		HRESULT hr = WHvGetVirtualProcessorRegisters(partitionHandle, 0, nn, 4, vv);
		if (hr != S_OK) {
			throw std::exception("Couldn't get register status");
		}

		unsigned int pending = (vv[1].PendingInterruption.InterruptionPending ? 1 : 0) | (vv[2].DeliverabilityNotifications.InterruptNotification ? 1 : 0);

		unsigned int mask = (1 << 22) | (1 << 23);
		mask = ~mask;

		// Update the RFLAGS
		unsigned int val = (vv[3].Reg32 & mask) | (pending << 22) | (halted << 23);
		/*	if (((val >> 9) & 1)) {
				if (!((vv[3].Reg64 >> 9) & 1)) {
					MessageBox(NULL, L"IRQ spuriously enabled!", L"Error", MB_OK);
				}
			} */

		jsobj->SetValue(L"run_loop_counter", CefV8Value::CreateUInt(run_loop_counter), V8_PROPERTY_ATTRIBUTE_NONE);
		jsobj->SetValue(L"io_counter", CefV8Value::CreateUInt(io_counter), V8_PROPERTY_ATTRIBUTE_NONE);
		jsobj->SetValue(L"irq_counter", CefV8Value::CreateUInt(irq_counter), V8_PROPERTY_ATTRIBUTE_NONE);
		jsobj->SetValue(L"mem_counter", CefV8Value::CreateUInt(mem_counter), V8_PROPERTY_ATTRIBUTE_NONE);
		jsobj->SetValue(L"inthandle_counter", CefV8Value::CreateUInt(inthandle_counter), V8_PROPERTY_ATTRIBUTE_NONE);
		return CefV8Value::CreateUInt(val);
	}


	/** Called to inject an IRQ */
	void irq(unsigned int irq)
	{
		irq_counter++;
		WHV_REGISTER_NAME nn[5] = {
	   WHvRegisterPendingInterruption, WHvX64RegisterDeliverabilityNotifications, WHvX64RegisterRflags,  WHvRegisterInterruptState, WHvRegisterPendingInterruption };
		WHV_REGISTER_VALUE vv[5];

		HRESULT hr = WHvGetVirtualProcessorRegisters(partitionHandle, 0, nn, 5, vv);
		if (hr != S_OK) {
			throw std::exception("Error raising IRQ");
		}

		unsigned int pending = (vv[0].PendingInterruption.InterruptionPending ? 1 : 0) | (vv[1].DeliverabilityNotifications.InterruptNotification ? 1 : 0);
		if (pending) {
			throw std::exception("New interrupt while interrupt pending");
		}
		if (!((vv[2].Reg64 >> 9) & 1)) {
			throw std::exception(
				"IRQ delivery without interrupts enabled (shouldn't "
				"happen)");
		}



		// Set the interrupt registers
		WHV_X64_PENDING_INTERRUPTION_REGISTER new_int;
		memset(&new_int, 0x0, sizeof(new_int));
		new_int.InterruptionType = WHvX64PendingInterrupt;
		new_int.InterruptionPending = 1;
		new_int.InterruptionVector = irq;

		WHV_REGISTER_NAME names[2] = {
		   WHvRegisterPendingInterruption, WHvX64RegisterDeliverabilityNotifications };
		WHV_REGISTER_VALUE values[2];
		memset(&values[0], 0x0, sizeof(values));
		values[0].PendingInterruption = new_int;
		values[1].DeliverabilityNotifications.InterruptNotification = 1;

		hr = WHvSetVirtualProcessorRegisters(partitionHandle, 0, names, 2, values);
		if (hr != S_OK) {
			throw std::exception("Error raising IRQ");
		}
	}

	unsigned char* getMemory() { return pMemory; }

	HRESULT HandleIO(WHV_EMULATOR_IO_ACCESS_INFO * IoAccess)
	{
		io_counter++;
		//CefRefPtr<> ret;
		CefString exception;
		CefV8ValueList list;
		parambuf[0] = IoAccess->Port;
		parambuf[1] = IoAccess->AccessSize;
		parambuf[2] = IoAccess->Direction;
		if (IoAccess->Direction) { // This is a write
			parambuf[3] = IoAccess->Data;
		}

		CefRefPtr<CefV8Value> retval;

		CefRefPtr<CefV8Value> cb = getIOCallback();
		retval = cb->ExecuteFunction(jsobj, list);

		if (!IoAccess->Direction) {
			// This is a read
			IoAccess->Data = parambuf[0];
		}
		return S_OK;
	}



	void InternalHandle(WHV_EMULATOR_MEMORY_ACCESS_INFO * MemoryAccess) {
		if (MemoryAccess->Direction) { // This is a write
			if (MemoryAccess->AccessSize == 1) {
				pMemory[MemoryAccess->GpaAddress] = *((unsigned char*)MemoryAccess->Data);
			}
			else if (MemoryAccess->AccessSize == 2) {
				*((unsigned short*)(pMemory + MemoryAccess->GpaAddress)) = *((unsigned short*)MemoryAccess->Data);
			}
			else if (MemoryAccess->AccessSize == 4) {
				*((unsigned int*)(pMemory + MemoryAccess->GpaAddress)) = *((unsigned int*)MemoryAccess->Data);
			}
		}
		else {
			if (MemoryAccess->AccessSize == 1) {
				*((unsigned char*)MemoryAccess->Data) = pMemory[MemoryAccess->GpaAddress];
			}
			else if (MemoryAccess->AccessSize == 2) {
				*((unsigned short*)MemoryAccess->Data) = *((unsigned short*)(pMemory + MemoryAccess->GpaAddress));
			}
			else if (MemoryAccess->AccessSize == 4) {
				*((unsigned int*)MemoryAccess->Data) = *((unsigned int*)(pMemory + MemoryAccess->GpaAddress));
			}
		}
	}

	CefRefPtr<CefV8Value> ioCallback;

	CefRefPtr<CefV8Value> getIOCallback()
	{
		if (ioCallback.get() == NULL) {
			ioCallback = jsobj->GetValue("iocallback");
		}
		return ioCallback;
	}

	CefV8ValueList empty_arg_list;

	HRESULT HandleMemory(WHV_EMULATOR_MEMORY_ACCESS_INFO * MemoryAccess)
	{
		mem_counter++;


		if (MemoryAccess->AccessSize == 8) {
			if (MemoryAccess->Direction) {
				unsigned char data[8];
				memcpy(data, MemoryAccess->Data, 8);
				MemoryAccess->AccessSize = 4;
				HRESULT hr = HandleMemory(MemoryAccess);
				if (hr != S_OK) {
					throw std::exception("Error");
				}
				memcpy(MemoryAccess->Data, data + 4, 4);
				MemoryAccess->GpaAddress += 4;
				hr = HandleMemory(MemoryAccess);
				if (hr != S_OK) {
					throw std::exception("Error");
				}
				MemoryAccess->GpaAddress -= 4;
				MemoryAccess->AccessSize = 8;
				memcpy(MemoryAccess->Data, data, 8);
				return S_OK;
			}
			else {
				// Read
				unsigned char data[8];
				MemoryAccess->AccessSize = 4;
				HRESULT hr = HandleMemory(MemoryAccess);
				if (hr != S_OK) {
					throw std::exception("Error");
				}

				memcpy(data, MemoryAccess->Data, 4);
				MemoryAccess->GpaAddress += 4;
				hr = HandleMemory(MemoryAccess);
				if (hr != S_OK) {
					throw std::exception("Error");
				}
				memcpy(data + 4, MemoryAccess->Data, 4);
				memcpy(MemoryAccess->Data, data, 8);
				MemoryAccess->GpaAddress -= 4;
				MemoryAccess->AccessSize = 8;
				return S_OK;
			}
		}


		CefString exception;
		unsigned int* p = (unsigned int*)parambuf;
		p[0] = MemoryAccess->GpaAddress;

		if (MemoryAccess->Direction) {
			// Write
			switch (MemoryAccess->AccessSize) {
			case 1:
				p[1] = *((unsigned char*)MemoryAccess->Data);
				mw1->ExecuteFunction(jscpu, empty_arg_list);
				break;
			case 2:
				p[1] = *((unsigned short*)MemoryAccess->Data);
				mw2->ExecuteFunction(jscpu, empty_arg_list);
				break;
			case 4:
				p[1] = *((unsigned int*)MemoryAccess->Data);
				mw4->ExecuteFunction(jscpu, empty_arg_list);
				break;
			}
		}
		else {
			// Read
			switch (MemoryAccess->AccessSize) {
			case 1:
				mr1->ExecuteFunction(jscpu, empty_arg_list);
				*((unsigned char*)MemoryAccess->Data) = (unsigned char)p[0];
				break;
			case 2:
				mr2->ExecuteFunction(jscpu, empty_arg_list);
				*((unsigned short*)MemoryAccess->Data) = (unsigned short)p[0];
				break;
			case 4:
				mr4->ExecuteFunction(jscpu, empty_arg_list);
				*((unsigned int*)MemoryAccess->Data) = (unsigned int)p[0];
				break;
			}
		}
		return S_OK;
	}



	HRESULT HandleSetRegisters(const WHV_REGISTER_NAME * RegisterNames,
		UINT32 RegisterCount,
		const WHV_REGISTER_VALUE * RegisterValues) {
		return WHvSetVirtualProcessorRegisters(partitionHandle, 0, RegisterNames,
			RegisterCount, RegisterValues);
	}

	HRESULT HandleGetRegisters(const WHV_REGISTER_NAME * RegisterNames,
		UINT32 RegisterCount,
		WHV_REGISTER_VALUE * RegisterValues) {
		return WHvGetVirtualProcessorRegisters(partitionHandle, 0, RegisterNames,
			RegisterCount, RegisterValues);
	}

	HRESULT HandleTranslateRange(WHV_GUEST_VIRTUAL_ADDRESS GvaPage,
		WHV_TRANSLATE_GVA_FLAGS TranslateFlags,
		WHV_TRANSLATE_GVA_RESULT_CODE * TranslationResult,
		WHV_GUEST_PHYSICAL_ADDRESS * GpaPage)
	{
		//WHvTranslateGva
		WHV_TRANSLATE_GVA_RESULT res;
		HRESULT hr = WHvTranslateGva(partitionHandle, 0x0, GvaPage, TranslateFlags, &res, GpaPage);
		*TranslationResult = (WHV_TRANSLATE_GVA_RESULT_CODE)res.ResultCode;
		return hr;
	}

	class UnmapEntry
	{
	public:
		size_t m_addr, m_sz;

		UnmapEntry(size_t addr, size_t sz) : m_addr(addr), m_sz(sz) {

		}
	};

	std::vector<UnmapEntry> unmaps;

	template<typename ... Args>
	std::wstring string_format(const std::wstring & format, Args ... args)
	{
		size_t size = _snwprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
		std::unique_ptr<WCHAR[]> buf(new WCHAR[size]);
		_snwprintf(buf.get(), size, format.c_str(), args ...);
		return std::wstring(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
	}


	void unmap(size_t addr, size_t sz)
	{
		unmaps.push_back(UnmapEntry(addr, sz));
		HRESULT hr = WHvUnmapGpaRange(partitionHandle, addr, sz);
		if (hr != S_OK) {
			throw std::exception("Couldn't unmap");
		}
	}

	HRESULT memmap(unsigned char* mem,
		size_t amount,
		unsigned int a20,
		unsigned int remap) {
		if (remap) {
			return S_OK;
		}
		// Amount must be in whole megabyte
		for (size_t i = 0; i < amount; i += 1024 * 1024) {
			unsigned char* target = mem + i;
			if (!a20) {
				if ((i / (1024 * 1024)) % 2 == 1) {
					// Odd MB without A20
					target -= 1024 * 1024;
				}
			}
			/*		if (remap) {
									HRESULT hr = WHvUnmapGpaRange(partitionHandle, i,
			   1024 * 1024); if (hr != S_OK) { return hr;
									}
							} */
			HRESULT hr =
				WHvMapGpaRange(partitionHandle, target, i, 1024 * 1024,
					WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite |
					WHvMapGpaRangeFlagExecute);
			if (hr != S_OK) {
				return hr;
			}
		}
		return S_OK;
	}

	void SetJSObject(CefRefPtr<CefV8Value> pJsobj)
	{
		jsobj = pJsobj;
	}

	unsigned char* GetParamBuf()
	{
		return (unsigned char*)this->parambuf;
	}


	IMPLEMENT_REFCOUNTING(CMachine);
};

