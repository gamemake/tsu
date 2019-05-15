#pragma once

#include "CoreMinimal.h"
#include "TsuInspectorSettings.generated.h"

UCLASS(config=Game, defaultconfig, ClassGroup=TSU)
class UTsuInspectorSettings final
	: public UObject
{
	GENERATED_BODY()

public:
	/** Whether or not to allow JavaScript code generation from strings (through eval or the Function constructor) */
	UPROPERTY(EditAnywhere, Config, Category="Runtime", Meta=(ConfigRestartRequired=true))
	int32 Port = 2016;
};
