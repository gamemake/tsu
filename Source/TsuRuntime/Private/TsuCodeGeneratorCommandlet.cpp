#include "TsuCodeGeneratorCommandlet.h"
#include "TsuCodeGenerator.h"

UTsuCodeGeneratorCommandlet::UTsuCodeGeneratorCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UTsuCodeGeneratorCommandlet::Main(const FString& Params)
{
    FTsuCodeGenerator::ExportAll();
	return 0;
}
