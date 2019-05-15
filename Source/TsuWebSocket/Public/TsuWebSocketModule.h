#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class TSUWEBSOCKET_API ITsuWebSocketModule
	: public IModuleInterface
{
public:
	static ITsuWebSocketModule& Get()
	{
		return FModuleManager::LoadModuleChecked<ITsuWebSocketModule>(TEXT("TsuWebSocket"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("TsuWebSocket"));
	}
};
