#include "TsuInspectorModule.h"
#include "TsuRuntimeModule.h"
#include "TsuInspector.h"
#include "TsuInspectorCallback.h"

static TOptional<FTsuInspectorClient> InspectorClient;

class FTsuInspectorModule final
	: public ITsuInspectorModule
	, public ITsuInspectorCallback
{
public:
	void StartupModule() override
	{
		InspectorClient.Emplace();
		InspectorClient->Start(1980);
		ITsuInspectorCallback::Set(this);
	}

	void ShutdownModule() override
	{
		ITsuInspectorCallback::Set(nullptr);
		InspectorClient->Stop();
	}

	void* CreateInspector(v8::Local<v8::Context> Context) override
	{
		auto Inspector = new FTsuInspector(Context, &InspectorClient.GetValue());
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
};

IMPLEMENT_MODULE(FTsuInspectorModule, TsuInspector)
