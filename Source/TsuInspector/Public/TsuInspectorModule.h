#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "TsuV8Wrapper.h"

class FTsuInspector;

class TSUINSPECTOR_API ITsuInspectorModule
	: public IModuleInterface
{
public:
	static ITsuInspectorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<ITsuInspectorModule>(TEXT("TsuInspector"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("TsuInspector"));
	}

	virtual FTsuInspector* CreateInspector(v8::Local<v8::Context> Context) = 0;
	virtual void DestroyInspector(FTsuInspector* Inpector) = 0;
};
