#include "CMachine.h"


HRESULT IoPortCallback(VOID* Context, WHV_EMULATOR_IO_ACCESS_INFO* IoAccess) {
	CMachine* pMachine = (CMachine*)Context;
	pMachine->HandleIO(IoAccess);
	return S_OK;
}

HRESULT SetVirtualRegisters(VOID* Context,
	const WHV_REGISTER_NAME* RegisterNames,
	UINT32 RegisterCount,
	const WHV_REGISTER_VALUE* RegisterValues) {
	CMachine* pMachine = (CMachine*)Context;
	return pMachine->HandleSetRegisters(RegisterNames, RegisterCount,
		RegisterValues);
}


HRESULT GetVirtualRegisters(VOID* Context,
	const WHV_REGISTER_NAME* RegisterNames,
	unsigned int RegisterCount,
	WHV_REGISTER_VALUE* RegisterValues) {
	CMachine* pMachine = (CMachine*)Context;
	return pMachine->HandleGetRegisters(RegisterNames, RegisterCount, RegisterValues);
}

HRESULT IoPortCallback(VOID* Context, WHV_EMULATOR_IO_ACCESS_INFO* IoAccess);
HRESULT SetVirtualRegisters(VOID* Context,
	const WHV_REGISTER_NAME* RegisterNames,
	UINT32 RegisterCount,
	const WHV_REGISTER_VALUE* RegisterValues);

HRESULT MemoryCallback(VOID* Context,
	WHV_EMULATOR_MEMORY_ACCESS_INFO* MemoryAccess) {
	CMachine* pMachine = (CMachine*)Context;
	pMachine->HandleMemory(MemoryAccess);
	return S_OK;
}

HRESULT TranslateRange(
	VOID* Context,
	WHV_GUEST_VIRTUAL_ADDRESS GvaPage,
	WHV_TRANSLATE_GVA_FLAGS TranslateFlags,
	WHV_TRANSLATE_GVA_RESULT_CODE* TranslationResult,
	WHV_GUEST_PHYSICAL_ADDRESS*
	GpaPage  // NOTE: This pointer _must_ be 4K page aligned
) {
	CMachine* pMachine = (CMachine*)Context;
	return pMachine->HandleTranslateRange(GvaPage, TranslateFlags, TranslationResult, GpaPage);
}

void StopperFunction(HANDLE partitionHandle, semaphore* sem)
{
	while (1) {
		sem->wait();
		/*		if (latchirq != -1) {
					WHV_INTERRUPT_CONTROL ctrl;
					memset(&ctrl, 0x0, sizeof(ctrl));
					ctrl.Type = WHvX64InterruptTypeFixed;
					ctrl.Vector = latchirq;
					ctrl.DestinationMode = WHvX64InterruptDestinationModePhysical;
					ctrl.TriggerMode = WHvX64InterruptTriggerModeLevel;
					ctrl.Destination = 0;
					HRESULT hr = WHvRequestInterrupt(
						partitionHandle, &ctrl, sizeof(ctrl));
					if (hr != S_OK) {
						MessageBox(NULL, L"ERROR", L"ERROR", MB_OK);
						//MessageBox(NULL, L"IRQError", L"IRQError", MB_OK);
						//throw std::exception("Error raising IRQ");
						continue;
					}
					else {
						MessageBox(NULL, L"OK", L"OK", MB_OK);
					}
				} */
		Sleep(1);
		for (int i = 0; i < 3; i++) {
			HRESULT hr = WHvCancelRunVirtualProcessor(partitionHandle, 0x0, 0);
			if (hr == S_OK) {
				break;
			}
			Sleep(1);
		}
	}
}
