#include "TsuCodeGenerator.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TsuPaths.h"
#include "TsuRuntimeLog.h"
#include "TsuUtilities.h"

void FTsuCodeGenerator::ExportAll()
{
    FTsuCodeGenerator Generator;

    Generator.WriteLine(TEXT("declare class TArray<T> {"));
    Generator.WriteLine(TEXT("    public empty(): void;"));
    Generator.WriteLine(TEXT("    public isEmpty(): boolean;"));
    Generator.WriteLine(TEXT("    public Add(value: T);"));
    Generator.WriteLine(TEXT("}"));
    Generator.WriteLine(TEXT("declare class TSet<T> {"));
    Generator.WriteLine(TEXT("    public contians(value: T): boolean;"));
    Generator.WriteLine(TEXT("    public remove(value: T): void;"));
    Generator.WriteLine(TEXT("    public add(value: T): void;"));
    Generator.WriteLine(TEXT("}"));
    Generator.WriteLine(TEXT("declare class TMap<K, V> {"));
    Generator.WriteLine(TEXT("    public contians(value: T): boolean;"));
    Generator.WriteLine(TEXT("    public remove(value: T): void;"));
    Generator.WriteLine(TEXT("    public add(value: T): void;"));
    Generator.WriteLine(TEXT("}"));

    for(TObjectIterator<UClass> It; It; ++It)
    {
        if (Generator.CanExport(*It))
        {
            Generator.Export(*It);
        }
    }

    auto Filename = FPaths::Combine(
        FPaths::ProjectContentDir(),
        TEXT(".."),
        TEXT("UE4.d.ts")
    );

    UE_LOG(LogTsuRuntime, Log, TEXT("OUT %s"), *Filename);
    FFileHelper::SaveStringArrayToFile(Generator.Lines, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FTsuCodeGenerator::CanExport(UClass *Class)
{
	if (IsFieldDeprecated(Class))
		return false;

    return true;
}

bool FTsuCodeGenerator::CanExport(UClass *Class, UFunction *Func)
{
	if (Func->FunctionFlags & FUNC_Delegate)
		return false;

	if (Func->FunctionFlags & FUNC_UbergraphFunction)
		return false;

	if (IsFieldDeprecated(Func))
		return false;

	if (Func->HasMetaData(TEXT("BlueprintGetter")) || Func->HasMetaData("BlueprintSetter"))
		return false;

	if (Func->GetName().StartsWith(TEXT("OnRep_")))
		return false;

    for(TFieldIterator<UProperty> PropIt(Func); PropIt; ++PropIt )
    {
        if (!CanExport(*PropIt))
        {
            return false;
        }
    }
    return true;
}

bool FTsuCodeGenerator::CanExport(UProperty *Prop)
{
	if (IsFieldDeprecated(Prop))
		return false;

    auto Type = PropertyType(Prop);
    return !Type.IsEmpty();
}

void FTsuCodeGenerator::Prepare(UProperty *Prop)
{
    if (auto EnumProperty = Cast<UEnumProperty>(Prop))
	{
        Export(EnumProperty->GetEnum());
	}
	else if (auto ObjectProperty = Cast<UObjectPropertyBase>(Prop))
	{
        Export(ObjectProperty->PropertyClass);
	}
	else if (auto StructProperty = Cast<UStructProperty>(Prop))
	{
        Export(StructProperty->Struct);
	}
	else if (auto ArrayProperty = Cast<UArrayProperty>(Prop))
	{
        Prepare(ArrayProperty->Inner);
	}
	else if (auto SetProperty = Cast<USetProperty>(Prop))
	{
        Prepare(SetProperty->ElementProp);
	}
	else if (auto MapProperty = Cast<UMapProperty>(Prop))
	{
        Prepare(MapProperty->KeyProp);
        Prepare(MapProperty->ValueProp);
	}
}

void FTsuCodeGenerator::Export(UClass *Class)
{
    if (Classes.Contains(Class))
    {
        return;
    }
    Classes.Add(Class);

    auto Super = Class->GetSuperClass();
    if (Super)
    {
        if (CanExport(Super))
        {
            Export(Super);
        }
        else
        {
            Super = nullptr;
        }
    }

    for(TFieldIterator<UProperty> PropIt(Class); PropIt; ++PropIt )
    {
        if (CanExport(*PropIt))
        {
            Prepare(*PropIt);
        }
    }

    for (TFieldIterator<UFunction> FuncIt(Class); FuncIt; ++FuncIt)
    {
        if (CanExport(Class, *FuncIt))
        {
            for(TFieldIterator<UProperty> PropIt(*FuncIt); PropIt; ++PropIt )
            {
                Prepare(*PropIt);
            }
        }
    }

    if (Super)
    {
        WriteLine(TEXT("declare class %s : %s {"), *ClassName(Class), *ClassName(Super));
    }
    else
    {
        WriteLine(TEXT("declare class %s {"), *ClassName(Class));
    }

    for(TFieldIterator<UProperty> PropIt(Class); PropIt; ++PropIt ) {
        if (CanExport(*PropIt))
        {
            Export(Class, *PropIt);
        }
    }

    for (TFieldIterator<UFunction> FuncIt(Class); FuncIt; ++FuncIt)
    {
        if (CanExport(Class, *FuncIt))
        {
            Export(Class, *FuncIt);
        }
    }

    WriteLine(TEXT("}"));
}

void FTsuCodeGenerator::Export(UClass *Class, UFunction *Func)
{
    FString Args;
    UProperty *ReturnProperty = nullptr;
    for(TFieldIterator<UProperty> PropIt(Func); PropIt; ++PropIt )
    {
        if ((*PropIt)->GetPropertyFlags() & CPF_ReturnParm)
        {
            ReturnProperty = *PropIt;
        }
        else
        {
            if (!Args.IsEmpty())
            {
                Args += ", ";
            }

            Args += FString::Printf(
                TEXT("%s: %s"),
                *PropertyName(*PropIt),
                *PropertyType(*PropIt)
            );
        }
    }

    if (ReturnProperty)
    {
        WriteLine(TEXT("    public %s(%s): %s;"), *Func->GetName(), *Args, *PropertyType(ReturnProperty));
    }
    else
    {
        WriteLine(TEXT("    public %s(%s): void;"), *Func->GetName(), *Args);
    }
}

void FTsuCodeGenerator::Export(UClass *Class, UProperty *Prop)
{
    WriteLine(
        TEXT("    public %s: %s;"),
        *PropertyName(Prop),
        *PropertyType(Prop)
    );
}

void FTsuCodeGenerator::Export(UStruct *Struct)
{
    if (Structs.Contains(Struct))
    {
        return;
    }
    Structs.Add(Struct);

    auto Super = Struct->GetSuperStruct();
    if (Super)
    {
        Export(Super);
    }

    for(TFieldIterator<UProperty> PropIt(Struct); PropIt; ++PropIt )
    {
        if (CanExport(*PropIt))
        {
            Prepare(*PropIt);
        }
    }

    if (Super)
    {
        WriteLine(TEXT("declare class %s : %s {"), *StructName(Struct), *StructName(Super));
    }
    else
    {
        WriteLine(TEXT("declare class %s {"), *StructName(Struct));
    }

    for(TFieldIterator<UProperty> PropIt(Struct); PropIt; ++PropIt ) {
        if (CanExport(*PropIt))
        {
            Export(Struct, *PropIt);
        }
    }

    WriteLine(TEXT("}"));
}

void FTsuCodeGenerator::Export(UStruct *Struct, UProperty *Prop)
{
    WriteLine(
        TEXT("    public %s: %s;"),
        *PropertyName(Prop),
        *PropertyType(Prop)
    );
}

void FTsuCodeGenerator::Export(UEnum* Enum)
{
    if (Enums.Contains(Enum))
    {
        return;
    }
    Enums.Add(Enum);

    WriteLine(TEXT("declare enum %s {"), *EnumName(Enum));
    for (int32 It = 0; It<Enum->NumEnums(); ++It)
    {
        WriteLine(
            TEXT("    %s = %d,"),
            *Enum->GetNameStringByIndex(It),
            Enum->GetValueByIndex(It)
        );
    }
    WriteLine(TEXT("}"));
}

const FString FTsuCodeGenerator::ClassName(UClass *Class)
{
    return Class->GetPrefixCPP() + Class->GetName();
}

const FString FTsuCodeGenerator::StructName(UStruct *Struct)
{
    return Struct->GetPrefixCPP() + Struct->GetName();
}

const FString FTsuCodeGenerator::EnumName(UEnum* Enum)
{
    return Enum->CppType;
}

const FString FTsuCodeGenerator::FunctionName(UFunction* Func)
{
    return Func->GetName();
}

const FString FTsuCodeGenerator::PropertyType(UProperty *Prop)
{
	if (auto StrProperty = Cast<UStrProperty>(Prop))
	{
        return TEXT("string");
	}
	else if (auto NameProperty = Cast<UNameProperty>(Prop))
	{
        return TEXT("string");
	}
	else if (auto TextProperty = Cast<UTextProperty>(Prop))
	{
        return TEXT("string");
	}
	else if (auto BoolProperty = Cast<UBoolProperty>(Prop))
	{
        return TEXT("boolean");
	}
	else if (auto NumericProperty = Cast<UNumericProperty>(Prop))
	{
        return TEXT("number");
	}
	else if (auto EnumProperty = Cast<UEnumProperty>(Prop))
	{
        return EnumName(EnumProperty->GetEnum());
	}
	else if (auto ObjectProperty = Cast<UObjectPropertyBase>(Prop))
	{
        auto Class = ObjectProperty->PropertyClass;
        if (CanExport(Class))
        {
            return ClassName(Class);
        }
        return TEXT("");
	}
	else if (auto StructProperty = Cast<UStructProperty>(Prop))
	{
        auto Struct = StructProperty->Struct;
        return StructName(Struct);
	}
	else if (auto ArrayProperty = Cast<UArrayProperty>(Prop))
	{
        auto Type = PropertyType(ArrayProperty->Inner);
        if (!Type.IsEmpty())
        {
            return FString::Printf(TEXT("TArray<%s>"), *Type);
        }
        return TEXT("");
	}
	else if (auto SetProperty = Cast<USetProperty>(Prop))
	{
        auto Type = PropertyType(SetProperty->ElementProp);
        if (!Type.IsEmpty())
        {
            return FString::Printf(TEXT("TSet<%s>"), *Type);
        }
        return TEXT("");
	}
	else if (auto MapProperty = Cast<UMapProperty>(Prop))
	{
        auto KeyType = PropertyType(MapProperty->KeyProp);
        auto ValueType = PropertyType(MapProperty->ValueProp);
        if (!KeyType.IsEmpty() && !ValueType.IsEmpty())
        {
            return FString::Printf(TEXT("TMap<%s, %s>"), *KeyType, *ValueType);
        }
        return TEXT("");
	}

    return TEXT("");
}

const FString FTsuCodeGenerator::PropertyName(UProperty *Prop)
{
    return Prop->GetName();
}

const FString FTsuCodeGenerator::PropertyDefault(UProperty *Prop)
{
    return TEXT("");
}

template <typename FmtType, typename... Types>
void FTsuCodeGenerator::WriteLine(const FmtType& Fmt, Types... Args)
{
    auto Line = FString::Printf(Fmt, Args...);
    if (!Line.IsEmpty())
    {
        for (auto It=0; It<TabStop; ++It)
        {
            Line = TEXT("    ") + Line;
        }
    }
    Lines.Add(Line);
}
