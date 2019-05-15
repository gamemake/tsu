#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

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
};
