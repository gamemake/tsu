#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "TsuV8Wrapper.h"

#define TSU_MODULE_RUNTIME TEXT("TsuRuntime")

class TSURUNTIME_API ITsuInspectorCallback
{
public:
	virtual ~ITsuInspectorCallback() {}

	virtual void* CreateInspector(v8::Local<v8::Context> Context) = 0;
	virtual void DestroyInspector(void* Inspector) = 0;
};

class TSURUNTIME_API ITsuRuntimeModule
	: public IModuleInterface
{
public:
	static ITsuRuntimeModule& Get()
	{
		return FModuleManager::LoadModuleChecked<ITsuRuntimeModule>(TSU_MODULE_RUNTIME);
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TSU_MODULE_RUNTIME);
	}

	virtual void SetInspectorCallback(ITsuInspectorCallback* Callback) = 0;
};
