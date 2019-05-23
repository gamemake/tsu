#include "TsuCodeGenerator.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/EnumProperty.h"
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

bool FTsuCodeGenerator::CanExport(UFunction *Function)
{
	if (Function->FunctionFlags & FUNC_Delegate)
		return false;

	if (Function->FunctionFlags & FUNC_UbergraphFunction)
		return false;

	if (IsFieldDeprecated(Function))
		return false;

	if (Function->HasMetaData(TEXT("BlueprintGetter")) || Function->HasMetaData("BlueprintSetter"))
		return false;

	if (Function->GetName().StartsWith(TEXT("OnRep_")))
		return false;

    for(TFieldIterator<UProperty> PropIt(Function); PropIt; ++PropIt )
    {
        if (!CanExport(*PropIt))
        {
            return false;
        }
    }
    return true;
}

bool FTsuCodeGenerator::CanExport(UProperty *Property)
{
	if (IsFieldDeprecated(Property))
		return false;

    auto Type = PropertyType(Property);
    return !Type.IsEmpty();
}

void FTsuCodeGenerator::Prepare(UProperty *Property)
{
    if (auto EnumProperty = Cast<UEnumProperty>(Property))
	{
        Export(EnumProperty->GetEnum());
	}
	else if (auto ObjectProperty = Cast<UObjectPropertyBase>(Property))
	{
        Export(ObjectProperty->PropertyClass);
	}
	else if (auto InterfaceProperty = Cast<UInterfaceProperty>(Property))
	{
        // Export(InterfaceProperty->);
	}
	else if (auto StructProperty = Cast<UStructProperty>(Property))
	{
        Export(StructProperty->Struct);
	}
	else if (auto ArrayProperty = Cast<UArrayProperty>(Property))
	{
        Prepare(ArrayProperty->Inner);
	}
	else if (auto SetProperty = Cast<USetProperty>(Property))
	{
        Prepare(SetProperty->ElementProp);
	}
	else if (auto MapProperty = Cast<UMapProperty>(Property))
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
        if (CanExport(*FuncIt))
        {
            for(TFieldIterator<UProperty> PropIt(*FuncIt); PropIt; ++PropIt )
            {
                Prepare(*PropIt);
            }
        }
    }

    if (Super)
    {
        WriteLine(TEXT("declare class %s extends %s {"), *ClassName(Class), *ClassName(Super));
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
        if (CanExport(*FuncIt))
        {
            Export(Class, *FuncIt);
        }
    }

    WriteLine(TEXT("}"));
}

void FTsuCodeGenerator::Export(UClass *Class, UFunction *Function)
{
    FString Args;
    UProperty *ReturnProperty = nullptr;
    for(TFieldIterator<UProperty> PropIt(Function); PropIt; ++PropIt )
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
        WriteLine(
            TEXT("    public %s%s(%s): %s;"),
            Function->FunctionFlags&FUNC_Static?TEXT(" static"):TEXT(""),
            *Function->GetName(),
            *Args,
            *PropertyType(ReturnProperty)
        );
    }
    else
    {
        WriteLine(
            TEXT("    public %s%s(%s): void;"),
            Function->FunctionFlags&FUNC_Static?TEXT(" static"):TEXT(""),
            *Function->GetName(),
            *Args
        );
    }
}

void FTsuCodeGenerator::Export(UClass *Class, UProperty *Property)
{
    WriteLine(
        TEXT("    public %s: %s;"),
        *PropertyName(Property),
        *PropertyType(Property)
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

    for (TFieldIterator<UFunction> FuncIt(Struct); FuncIt; ++FuncIt)
    {
        if (CanExport(*FuncIt))
        {
            for(TFieldIterator<UProperty> PropIt(*FuncIt); PropIt; ++PropIt )
            {
                Prepare(*PropIt);
            }
        }
    }

    if (Super)
    {
        WriteLine(TEXT("declare class %s extends %s {"), *StructName(Struct), *StructName(Super));
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

    for (TFieldIterator<UFunction> FuncIt(Struct); FuncIt; ++FuncIt)
    {
        if (CanExport(*FuncIt))
        {
            Export(nullptr, *FuncIt);
        }
    }

    WriteLine(TEXT("}"));
}

void FTsuCodeGenerator::Export(UStruct *Struct, UProperty *Property)
{
    WriteLine(
        TEXT("    public %s: %s;"),
        *PropertyName(Property),
        *PropertyType(Property)
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

const FString FTsuCodeGenerator::FunctionName(UFunction* Function)
{
    return Function->GetName();
}

const FString FTsuCodeGenerator::PropertyType(UProperty *Property)
{
	if (auto StrProperty = Cast<UStrProperty>(Property))
	{
        return TEXT("string");
	}
	else if (auto NameProperty = Cast<UNameProperty>(Property))
	{
        return TEXT("string");
	}
	else if (auto TextProperty = Cast<UTextProperty>(Property))
	{
        return TEXT("string");
	}
	else if (auto BoolProperty = Cast<UBoolProperty>(Property))
	{
        return TEXT("boolean");
	}
	else if (auto NumericProperty = Cast<UNumericProperty>(Property))
	{
        return TEXT("number");
	}
	else if (auto EnumProperty = Cast<UEnumProperty>(Property))
	{
        return EnumName(EnumProperty->GetEnum());
	}
	else if (auto ObjectProperty = Cast<UObjectPropertyBase>(Property))
	{
        auto Class = ObjectProperty->PropertyClass;
        if (CanExport(Class))
        {
            return ClassName(Class);
        }
        return TEXT("");
	}
	else if (auto StructProperty = Cast<UStructProperty>(Property))
	{
        auto Struct = StructProperty->Struct;
        return StructName(Struct);
	}
	else if (auto ArrayProperty = Cast<UArrayProperty>(Property))
	{
        auto Type = PropertyType(ArrayProperty->Inner);
        if (!Type.IsEmpty())
        {
            return FString::Printf(TEXT("TArray<%s>"), *Type);
        }
        return TEXT("");
	}
	else if (auto SetProperty = Cast<USetProperty>(Property))
	{
        auto Type = PropertyType(SetProperty->ElementProp);
        if (!Type.IsEmpty())
        {
            return FString::Printf(TEXT("TSet<%s>"), *Type);
        }
        return TEXT("");
	}
	else if (auto MapProperty = Cast<UMapProperty>(Property))
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

const FString FTsuCodeGenerator::PropertyName(UProperty *Property)
{
    return Property->GetName();
}

const FString FTsuCodeGenerator::PropertyDefault(UProperty *Property)
{
    return TEXT("");
}

template <typename FmtType, typename... Types>
void FTsuCodeGenerator::WriteLine(const FmtType& Fmt, Types... Args)
{
    auto Line = FString::Printf(Fmt, Args...);
    Lines.Add(Line);
}
