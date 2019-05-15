#pragma once

#include "CoreMinimal.h"

#include "TsuV8Wrapper.h"

class TSURUNTIME_API FTsuIsolate
{
public:
	static v8::Isolate* Get();
	static v8::Platform* GetPlatform();
};
