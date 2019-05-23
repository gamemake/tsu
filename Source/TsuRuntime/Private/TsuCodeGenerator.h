#pragma once

#include "CoreMinimal.h"

class TSURUNTIME_API FTsuCodeGenerator
{
public:
    static void ExportAll();

    bool CanExport(UClass *Class);
    bool CanExport(UClass *Class, UFunction *Func);
    bool CanExport(UProperty *Prop);

    void Prepare(UProperty *Prop);

    void Export(UClass *Class);
    void Export(UClass *Class, UFunction *Func);
    void Export(UClass *Class, UProperty *Prop);
    void Export(UStruct *Struct);
    void Export(UStruct *Struct, UProperty *Prop);
    void Export(UEnum* Enum);

    const FString ClassName(UClass *Class);
    const FString StructName(UStruct *Struct);
    const FString EnumName(UEnum* Enum);
    const FString FunctionName(UFunction* Func);
    const FString PropertyType(UProperty *Prop);
    const FString PropertyName(UProperty *Prop);
    const FString PropertyDefault(UProperty *Prop);

    TSet<UClass*> Classes;
    TSet<UStruct*> Structs;
    TSet<UEnum*> Enums;

	template <typename FmtType, typename... Types>
    void WriteLine(const FmtType& Fmt, Types... Args);
    int TabStop = 0;
    TArray<FString> Lines;
};
