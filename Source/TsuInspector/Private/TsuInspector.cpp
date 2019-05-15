#include "TsuInspector.h"

#include "TsuInspectorClient.h"
#include "TsuIsolate.h"

FTsuInspector::FTsuInspector(v8::Local<v8::Context> Context, FTsuInspectorClient* Client)
	: InpectorClient(Client)
{
	v8::Isolate* Isolate = FTsuIsolate::Get();
	v8::Platform* Platform = FTsuIsolate::GetPlatform();
	GlobalContext.Reset(Isolate, Context);

	InpectorClient->RegisterContext(Context);
}

FTsuInspector::~FTsuInspector()
{
	v8::Isolate* Isolate = FTsuIsolate::Get();
	v8::HandleScope HandleScope{Isolate};
	InpectorClient->UnregisterContext(GlobalContext.Get(Isolate));
}
