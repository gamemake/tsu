#pragma once

#include "CoreMinimal.h"

class TSURUNTIME_API FTsuCodeGenerator
{
public:
    static void ExportAll();

private:
    FTsuCodeGenerator() {}
    ~FTsuCodeGenerator() {}
    
    bool CanExport(UClass *Class);
    bool CanExport(UFunction *Function);
    bool CanExport(UProperty *Property);
    void Prepare(UProperty *Property);

    void Export(UClass *Class);
    void Export(UClass *Class, UFunction *Function);
    void Export(UClass *Class, UProperty *Property);
    void Export(UStruct *Struct);
    void Export(UStruct *Struct, UProperty *Property);
    void Export(UEnum* Enum);

    const FString ClassName(UClass *Class);
    const FString StructName(UStruct *Struct);
    const FString EnumName(UEnum* Enum);
    const FString FunctionName(UFunction* Function);
    const FString PropertyType(UProperty *Property);
    const FString PropertyName(UProperty *Property);
    const FString PropertyDefault(UProperty *Property);

    TSet<UClass*> Classes;
    TSet<UStruct*> Structs;
    TSet<UEnum*> Enums;

	template <typename FmtType, typename... Types>
    void WriteLine(const FmtType& Fmt, Types... Args);
    int TabStop = 0;
    TArray<FString> Lines;
};
