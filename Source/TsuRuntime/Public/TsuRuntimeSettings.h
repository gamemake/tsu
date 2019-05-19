#pragma once

#include "CoreMinimal.h"
#include "TsuRuntimeSettings.generated.h"

UCLASS(config=Game, defaultconfig, ClassGroup=TSU)
class TSURUNTIME_API UTsuRuntimeSettings final
	: public UObject
{
	GENERATED_BODY()

public:
	/** Whether or not to allow JavaScript code generation from strings (through eval or the Function constructor) */
	UPROPERTY(EditAnywhere, Config, Category="Runtime", Meta=(ConfigRestartRequired=true))
	bool bAllowCodeGenerationFromStrings = false;

	UPROPERTY(EditAnywhere, Config, Category="Inspector", Meta=(ConfigRestartRequired=true))
	int32 Port = 19800;

	UPROPERTY(EditAnywhere, Config, Category="Parser", Meta=(ConfigRestartRequired=false))
	float ParserTimeout = 5.f;

	/** Whether or not to use a DefaultToSelf parameter */
	UPROPERTY(EditAnywhere, Config, Category="Compilation", Meta=(ConfigRestartRequired=true))
	bool bUseSelfParameter = false;

	/** The name of the parameter that should get the 'DefaultToSelf' metadata assigned to it */
	UPROPERTY(EditAnywhere, Config, Category="Compilation", Meta=(ConfigRestartRequired=true, EditCondition="bUseSelfParameter"))
	FString SelfParameterName = TEXT("target");

};
