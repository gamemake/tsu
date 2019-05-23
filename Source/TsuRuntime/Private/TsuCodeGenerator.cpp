#include "TsuCodeGenerator.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "TsuRuntimeLog.h"

void FTsuCodeGenerator::ExportAll()
{
    FTsuCodeGenerator Generator;
    for(TObjectIterator<UClass> It; It; ++It)
    {
        if (Generator.CanExport(*It))
        {
            Generator.ExportClass(*It);
        }
    }
}

bool FTsuCodeGenerator::CanExport(UClass *Class)
{
    return true;
}

void FTsuCodeGenerator::ExportClass(UClass *Class)
{
    UE_LOG(LogTsuRuntime, Log, TEXT("Class %s"), *Class->GetName());

    for(TFieldIterator<UProperty> Prop(Class); Prop; ++Prop ) {
        UE_LOG(LogTsuRuntime, Log, TEXT(" -- %s"), *Prop->GetName());
    }
}
