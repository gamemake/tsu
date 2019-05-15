#pragma once

#include "CoreMinimal.h"
#include "TsuV8Wrapper.h"

class FTsuInspectorClient;

class FTsuInspector
{
public:
	FTsuInspector(v8::Local<v8::Context> Context, FTsuInspectorClient* Client);
	~FTsuInspector();

	FTsuInspector(const FTsuInspector& Other) = delete;
	FTsuInspector& operator=(const FTsuInspector& Other) = delete;

private:
	v8::Global<v8::Context> GlobalContext;
	FTsuInspectorClient* InpectorClient;
};
