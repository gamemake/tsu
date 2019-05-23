#pragma once

#include "CoreMinimal.h"

class TSURUNTIME_API FTsuCodeGenerator
{
public:
    static void ExportAll();

    bool CanExport(UClass *Class);
    void ExportClass(UClass *Class);
};
