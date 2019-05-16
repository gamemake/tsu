#pragma once

#include "CoreMinimal.h"
#include "TsuV8Wrapper.h"

class FTsuRuntimeModule;

class TSURUNTIME_API FTsuIsolate
{
	friend class FTsuRuntimeModule;
protected:
	static void Initialize();
	static void Uninitialize();

public:
	static v8::Isolate* Get() { return Isolate; }
	static v8::Platform* GetPlatform() { return Platform.get(); }

private:
	static v8::Isolate* Isolate;
	static std::unique_ptr<v8::Platform> Platform;
};
