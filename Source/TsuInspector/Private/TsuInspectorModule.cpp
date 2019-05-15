#include "TsuInspectorModule.h"
#include "TsuInspector.h"

class FTsuInspectorModule final : public ITsuInspectorModule
{
public:
	void StartupModule() override
	{
		Inspector.Start(1980);
	}

	void ShutdownModule() override
	{
		Inspector.Stop();
	}

	FTsuInspector* CreateInspector(v8::Local<v8::Context> Context) override
	{
		return new FTsuInspector(Context, &Inspector);
	}
	
	void DestroyInspector(FTsuInspector* Inpector) override
	{

	}

	FTsuInspectorClient Inspector;
};

IMPLEMENT_MODULE(FTsuInspectorModule, TsuInspector)
