#include "TsuCodeGenerateCommandlet.h"
#include "TsuCodeGenerator.h"

UTsuCodeGenerateCommandlet::UTsuCodeGenerateCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UTsuCodeGenerateCommandlet::Main(const FString& Params)
{
    FTsuCodeGenerator::ExportAll();
	return 0;
}
