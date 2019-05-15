#include "TsuInspectorModule.h"
#include "TsuRuntimeModule.h"
#include "TsuInspector.h"

class FTsuInspectorModule final
	: public ITsuInspectorModule
	, public ITsuInspectorCallback
{
public:
	void StartupModule() override
	{
		InspectorClient.Start(1980);
	}

	void ShutdownModule() override
	{
		InspectorClient.Stop();
	}

	void* CreateInspector(v8::Local<v8::Context> Context) override
	{
		auto Inspector = new FTsuInspector(Context, &InspectorClient);
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

	FTsuInspectorClient InspectorClient;
	TMap<void*, FTsuInspector*> Inspectors;
};

IMPLEMENT_MODULE(FTsuInspectorModule, TsuInspector)
