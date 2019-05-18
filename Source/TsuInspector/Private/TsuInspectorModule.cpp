#include "TsuInspectorModule.h"
#include "TsuRuntimeModule.h"
#include "TsuInspector.h"
#include "TsuInspectorCallback.h"

class FTsuInspectorModule final
	: public ITsuInspectorModule
	, public ITsuInspectorCallback
{
public:
	void StartupModule() override
	{
		ITsuInspectorCallback::Set(this);
	}

	void ShutdownModule() override
	{
		ITsuInspectorCallback::Set(nullptr);
	}

	void InitializeInspectorServer(int Port)
	{
		InspectorClient = new FTsuInspectorClient;
		InspectorClient->Start(1980);
	}

	void UninitializeInspectorServer()
	{
		InspectorClient->Stop();
		delete InspectorClient;
	}

	void* CreateInspector(v8::Local<v8::Context> Context) override
	{
		auto Inspector = new FTsuInspector(Context, InspectorClient);
		Inspectors.Add(Inspector, Inspector);
		return Inspector;
	}
	
	void DestroyInspector(void* Inpector) override
	{
		FTsuInspector* Value = nullptr;
		if (Inspectors.RemoveAndCopyValue(Inpector, Value))
		{
			delete Value;
		}
	}

	TMap<void*, FTsuInspector*> Inspectors;
	FTsuInspectorClient* InspectorClient;
};

IMPLEMENT_MODULE(FTsuInspectorModule, TsuInspector)
