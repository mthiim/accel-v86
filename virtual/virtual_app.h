// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_TESTS_CEFVIRTUAL_VIRTUAL_APP_H_
#define CEF_TESTS_CEFVIRTUAL_VIRTUAL_APP_H_

#include "CMachine.h"
#include "include/cef_app.h"

// Implement application-level callbacks for the browser process.
class VirtualApp : public CefApp,
	public CefBrowserProcessHandler,
	public CefRenderProcessHandler,
	public CefV8Handler,
	public CefV8ArrayBufferReleaseCallback {
public:
	VirtualApp();

	// CefApp methods:
	virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()
		OVERRIDE {
		return this;
	}

	virtual CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler()
		OVERRIDE {
		return this;
	}

#define GETMACHINE(x) ((CMachine*)x->GetUserData().get())

	virtual bool Execute(const CefString& name,
		CefRefPtr<CefV8Value> object,
		const CefV8ValueList& arguments,
		CefRefPtr<CefV8Value>& retval,
		CefString& exception) OVERRIDE {
		try {
			if (name == "run") {
				retval = GETMACHINE(object)->run();
				return true;
			}
			else if (name == "irq") {
				GETMACHINE(object)->irq(arguments[0]->GetUIntValue());
				return true;
			}
			else if (name == "unmap") {
				size_t addr = arguments[0]->GetUIntValue();
				size_t sz = arguments[1]->GetUIntValue();
				GETMACHINE(object)->unmap(addr, sz);
				return true;
			}
			else if (name == "StartMachine") {
				uint32 memorySize = arguments[0]->GetUIntValue();
				CefRefPtr<CefV8Value> cpu = arguments[1];
				CefRefPtr<CefV8Value> mw1 = arguments[2];
				CefRefPtr<CefV8Value> mw2 = arguments[3];
				CefRefPtr<CefV8Value> mw4 = arguments[4];
				CefRefPtr<CefV8Value> mr1 = arguments[5];
				CefRefPtr<CefV8Value> mr2 = arguments[6];
				CefRefPtr<CefV8Value> mr4 = arguments[7];

				// std::shared_ptr<CMachine> machine = std::make_shared<CMachine>(sz,
				// cpu, mw1, mw2, mw4, mr1, mr2, mr4);
				CefRefPtr<CMachine> pMachine = CefRefPtr<CMachine>(
					new CMachine(memorySize, cpu, mw1, mw2, mw4, mr1, mr2, mr4));

				// Create return object containing refernece to memory, callback
				// functions etc.
				CefRefPtr<CefV8Value> obj = CefV8Value::CreateObject(NULL, NULL);

				obj->SetUserData(pMachine);
				pMachine->SetJSObject(obj);

				CefRefPtr<CefV8Value> memory = CefV8Value::CreateArrayBuffer(
					pMachine->getMemory(), memorySize, this);
				obj->SetValue("memory", memory, V8_PROPERTY_ATTRIBUTE_NONE);

				CefRefPtr<CefV8Value> func_run =
					CefV8Value::CreateFunction("run", this);
				obj->SetValue("run", func_run, V8_PROPERTY_ATTRIBUTE_NONE);

				CefRefPtr<CefV8Value> func_irq =
					CefV8Value::CreateFunction("irq", this);
				obj->SetValue("irq", func_irq, V8_PROPERTY_ATTRIBUTE_NONE);

				CefRefPtr<CefV8Value> func_unmap =
					CefV8Value::CreateFunction("unmap", this);
				obj->SetValue("unmap", func_unmap, V8_PROPERTY_ATTRIBUTE_NONE);

				CefRefPtr<CefV8Value> parambuf =
					CefV8Value::CreateArrayBuffer(pMachine->GetParamBuf(), 4096, this);
				obj->SetValue("parambuf", parambuf, V8_PROPERTY_ATTRIBUTE_NONE);
				retval = obj;

				return true;
			}
		}
		catch (std::exception & ex) {
			exception = ex.what();
			return true;
		}

		// Function does not exist.
		return false;
	}

	virtual void ReleaseBuffer(void* buffer) OVERRIDE {}

	// CefBrowserProcessHandler methods:
	virtual void OnContextInitialized() OVERRIDE;

private:
	void OnContextCreated(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefV8Context> context) OVERRIDE;

	// Include the default reference counting implementation.
	IMPLEMENT_REFCOUNTING(VirtualApp);
};

#endif  // CEF_TESTS_CEFVIRTUAL_VIRTUAL_APP_H_
