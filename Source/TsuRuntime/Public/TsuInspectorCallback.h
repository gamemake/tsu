#pragma once

#include "TsuV8Wrapper.h"

class TSURUNTIME_API ITsuInspectorCallback
{
public:
	static void Set(ITsuInspectorCallback* Callback);
	static ITsuInspectorCallback* Get();

	virtual ~ITsuInspectorCallback() {}

	virtual void InitializeInspectorServer(int Port) = 0;
	virtual void UninitializeInspectorServer() = 0;
	virtual void* CreateInspector(v8::Local<v8::Context> Context) = 0;
	virtual void DestroyInspector(void* Inspector) = 0;
};
