#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"

#include "TestWebSocketCommandlet.generated.h"

UCLASS()
class UTestWebSocketCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
