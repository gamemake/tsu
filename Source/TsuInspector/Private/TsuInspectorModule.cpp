#include "TsuInspectorModule.h"

class FTsuInspectorModule final : public ITsuInspectorModule
{
public:
	void StartupModule() override
	{
	}

	void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FTsuInspectorModule, TsuInspector)
