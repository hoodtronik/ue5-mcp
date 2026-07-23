// CLAUDE-NOTE: PCG graph-authoring endpoints. run_python / the reflected Python API cannot (a) add
// graph user-parameters (the FInstancedPropertyBag exposes no add-property to Python) nor (b) wire a
// node property's override pin to a parameter. Both are needed for native parameter-driven PCG (e.g. a
// Blueprint driving per-mesh Count/Size on a scatter graph). These are implemented against PCG's editor
// C++ API. Batched cluster: add / list / remove / set graph user-parameters + bind a property override
// to a parameter (creates a UserParameterGet node and connects it to the target node's override pin).
// Mutations MarkPackageDirty + Modify() — persist by saving the asset (run_python / Save All).

#include "BlueprintMCPServer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Elements/PCGUserParameterGet.h"
#include "StructUtils/PropertyBag.h"
#include "Misc/PackageName.h"

namespace
{
	UPCGGraph* LoadPCGGraphByPath(const FString& InPath)
	{
		FString ObjPath = InPath;
		// Accept either "/Game/PCG/Foo" or "/Game/PCG/Foo.Foo".
		if (!ObjPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(ObjPath);
			ObjPath = ObjPath + TEXT(".") + AssetName;
		}
		return LoadObject<UPCGGraph>(nullptr, *ObjPath);
	}

	bool ParseBagType(const FString& InType, EPropertyBagPropertyType& OutType, const UObject*& OutTypeObject)
	{
		OutTypeObject = nullptr;
		const FString L = InType.ToLower();
		if (L == TEXT("bool"))                        { OutType = EPropertyBagPropertyType::Bool;   return true; }
		if (L == TEXT("int") || L == TEXT("int32"))   { OutType = EPropertyBagPropertyType::Int32;  return true; }
		if (L == TEXT("float"))                       { OutType = EPropertyBagPropertyType::Float;  return true; }
		if (L == TEXT("double"))                      { OutType = EPropertyBagPropertyType::Double; return true; }
		if (L == TEXT("name"))                        { OutType = EPropertyBagPropertyType::Name;   return true; }
		if (L == TEXT("string"))                      { OutType = EPropertyBagPropertyType::String; return true; }
		if (L == TEXT("vector"))                      { OutType = EPropertyBagPropertyType::Struct; OutTypeObject = TBaseStructure<FVector>::Get(); return true; }
		return false;
	}

	FString BagTypeToString(const FPropertyBagPropertyDesc& Desc)
	{
		switch (Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:   return TEXT("bool");
		case EPropertyBagPropertyType::Int32:  return TEXT("int32");
		case EPropertyBagPropertyType::Float:  return TEXT("float");
		case EPropertyBagPropertyType::Double: return TEXT("double");
		case EPropertyBagPropertyType::Name:   return TEXT("name");
		case EPropertyBagPropertyType::String: return TEXT("string");
		case EPropertyBagPropertyType::Struct: return Desc.ValueTypeObject ? Desc.ValueTypeObject->GetName() : TEXT("struct");
		default:                               return TEXT("other");
		}
	}

	// Apply a JSON value (field InField) to an existing graph user-parameter, matching its type.
	// CLAUDE-NOTE: routes through the public UPCGGraph::SetGraphParameter<T> (GetMutableUserParametersStruct
	// is protected), which also fires the graph's parameter-changed notification.
	void SetGraphValueFromJson(UPCGGraph* Graph, const FPropertyBagPropertyDesc& Desc,
		const TSharedPtr<FJsonObject>& Json, const FString& InField)
	{
		const FName N = Desc.Name;
		double NumV = 0.0; bool BoolV = false; FString StrV;
		switch (Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:   if (Json->TryGetBoolField(InField, BoolV))   Graph->SetGraphParameter(N, BoolV); break;
		case EPropertyBagPropertyType::Int32:  if (Json->TryGetNumberField(InField, NumV))  Graph->SetGraphParameter(N, (int32)NumV); break;
		case EPropertyBagPropertyType::Float:  if (Json->TryGetNumberField(InField, NumV))  Graph->SetGraphParameter(N, (float)NumV); break;
		case EPropertyBagPropertyType::Double: if (Json->TryGetNumberField(InField, NumV))  Graph->SetGraphParameter(N, NumV); break;
		case EPropertyBagPropertyType::Name:   if (Json->TryGetStringField(InField, StrV))  Graph->SetGraphParameter(N, FName(*StrV)); break;
		case EPropertyBagPropertyType::String: if (Json->TryGetStringField(InField, StrV))  Graph->SetGraphParameter(N, StrV); break;
		case EPropertyBagPropertyType::Struct:
		{
			const TSharedPtr<FJsonObject>* VObj = nullptr;
			if (Json->TryGetObjectField(InField, VObj) && VObj)
			{
				const FVector V((*VObj)->GetNumberField(TEXT("x")), (*VObj)->GetNumberField(TEXT("y")), (*VObj)->GetNumberField(TEXT("z")));
				Graph->SetGraphParameter(N, V);
			}
			break;
		}
		default: break;
		}
	}
}

FString FBlueprintMCPServer::HandlePcgAddUserParam(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body."));

	FString GraphPath, Name, TypeStr;
	if (!Json->TryGetStringField(TEXT("graph"), GraphPath) || GraphPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: 'graph'."));
	if (!Json->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())            return MakeErrorJson(TEXT("Missing required field: 'name'."));
	if (!Json->TryGetStringField(TEXT("type"), TypeStr) || TypeStr.IsEmpty())      return MakeErrorJson(TEXT("Missing required field: 'type'."));

	UPCGGraph* Graph = LoadPCGGraphByPath(GraphPath);
	if (!Graph) return MakeErrorJson(FString::Printf(TEXT("PCG graph not found: %s"), *GraphPath));

	EPropertyBagPropertyType BagType; const UObject* TypeObj = nullptr;
	if (!ParseBagType(TypeStr, BagType, TypeObj))
		return MakeErrorJson(FString::Printf(TEXT("Unsupported type '%s' (use bool/int/float/double/name/string/vector)."), *TypeStr));

	if (const FInstancedPropertyBag* Existing = Graph->GetUserParametersStruct())
	{
		if (Existing->FindPropertyDescByName(FName(*Name)) != nullptr)
			return MakeErrorJson(FString::Printf(TEXT("Parameter '%s' already exists on this graph."), *Name));
	}

	Graph->Modify();
	FPropertyBagPropertyDesc Desc(FName(*Name), BagType, TypeObj);
	Graph->AddUserParameters({ Desc });

	if (Json->HasField(TEXT("default")))
	{
		if (const FInstancedPropertyBag* Bag = Graph->GetUserParametersStruct())
		{
			if (const FPropertyBagPropertyDesc* Added = Bag->FindPropertyDescByName(FName(*Name)))
			{
				SetGraphValueFromJson(Graph, *Added, Json, TEXT("default"));
			}
		}
	}
	Graph->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("parameter"), Name);
	Result->SetStringField(TEXT("type"), TypeStr);
	return JsonToString(Result);
}

FString FBlueprintMCPServer::HandlePcgListUserParams(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body."));

	FString GraphPath;
	if (!Json->TryGetStringField(TEXT("graph"), GraphPath) || GraphPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: 'graph'."));

	UPCGGraph* Graph = LoadPCGGraphByPath(GraphPath);
	if (!Graph) return MakeErrorJson(FString::Printf(TEXT("PCG graph not found: %s"), *GraphPath));

	TArray<TSharedPtr<FJsonValue>> Arr;
	if (const FInstancedPropertyBag* Bag = Graph->GetUserParametersStruct())
	{
		if (const UPropertyBag* Struct = Bag->GetPropertyBagStruct())
		{
			for (const FPropertyBagPropertyDesc& D : Struct->GetPropertyDescs())
			{
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("name"), D.Name.ToString());
				O->SetStringField(TEXT("type"), BagTypeToString(D));
				Arr.Add(MakeShared<FJsonValueObject>(O));
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("parameters"), Arr);
	return JsonToString(Result);
}

FString FBlueprintMCPServer::HandlePcgRemoveUserParam(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body."));

	FString GraphPath, Name;
	if (!Json->TryGetStringField(TEXT("graph"), GraphPath) || GraphPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: 'graph'."));
	if (!Json->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())            return MakeErrorJson(TEXT("Missing required field: 'name'."));

	UPCGGraph* Graph = LoadPCGGraphByPath(GraphPath);
	if (!Graph) return MakeErrorJson(FString::Printf(TEXT("PCG graph not found: %s"), *GraphPath));

	const FInstancedPropertyBag* Bag = Graph->GetUserParametersStruct();
	if (!Bag || Bag->FindPropertyDescByName(FName(*Name)) == nullptr)
		return MakeErrorJson(FString::Printf(TEXT("Parameter '%s' not found."), *Name));

	Graph->Modify();
	Graph->UpdateUserParametersStruct([&](FInstancedPropertyBag& MutableBag)
	{
		MutableBag.RemovePropertyByName(FName(*Name));
	});
	Graph->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed"), Name);
	return JsonToString(Result);
}

FString FBlueprintMCPServer::HandlePcgSetUserParam(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body."));

	FString GraphPath, Name;
	if (!Json->TryGetStringField(TEXT("graph"), GraphPath) || GraphPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: 'graph'."));
	if (!Json->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())            return MakeErrorJson(TEXT("Missing required field: 'name'."));

	UPCGGraph* Graph = LoadPCGGraphByPath(GraphPath);
	if (!Graph) return MakeErrorJson(FString::Printf(TEXT("PCG graph not found: %s"), *GraphPath));

	const FInstancedPropertyBag* Bag = Graph->GetUserParametersStruct();
	const FPropertyBagPropertyDesc* Desc = Bag ? Bag->FindPropertyDescByName(FName(*Name)) : nullptr;
	if (!Desc) return MakeErrorJson(FString::Printf(TEXT("Parameter '%s' not found."), *Name));

	Graph->Modify();
	SetGraphValueFromJson(Graph, *Desc, Json, TEXT("value"));
	Graph->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("parameter"), Name);
	return JsonToString(Result);
}

FString FBlueprintMCPServer::HandlePcgBindOverride(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body."));

	FString GraphPath, NodeName, Property, ParamName;
	if (!Json->TryGetStringField(TEXT("graph"), GraphPath) || GraphPath.IsEmpty())   return MakeErrorJson(TEXT("Missing required field: 'graph'."));
	if (!Json->TryGetStringField(TEXT("node"), NodeName) || NodeName.IsEmpty())      return MakeErrorJson(TEXT("Missing required field: 'node' (UPCGNode object name)."));
	if (!Json->TryGetStringField(TEXT("property"), Property) || Property.IsEmpty())   return MakeErrorJson(TEXT("Missing required field: 'property' (override pin label, e.g. 'CellSize')."));
	if (!Json->TryGetStringField(TEXT("parameter"), ParamName) || ParamName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: 'parameter' (graph user-parameter name)."));

	UPCGGraph* Graph = LoadPCGGraphByPath(GraphPath);
	if (!Graph) return MakeErrorJson(FString::Printf(TEXT("PCG graph not found: %s"), *GraphPath));

	UPCGNode* Target = nullptr;
	for (UPCGNode* N : Graph->GetNodes())
	{
		if (N && N->GetName() == NodeName) { Target = N; break; }
	}
	if (!Target) return MakeErrorJson(FString::Printf(TEXT("Target node '%s' not found in graph."), *NodeName));

	const FInstancedPropertyBag* Bag = Graph->GetUserParametersStruct();
	const FPropertyBagPropertyDesc* Pd = Bag ? Bag->FindPropertyDescByName(FName(*ParamName)) : nullptr;
	if (!Pd) return MakeErrorJson(FString::Printf(TEXT("Graph user-parameter '%s' not found. Add it first."), *ParamName));

	Graph->Modify();
	UPCGUserParameterGetSettings* GetSettings = nullptr;
	UPCGNode* GetNode = Graph->AddNodeOfType(GetSettings);
	if (!GetNode || !GetSettings) return MakeErrorJson(TEXT("Failed to create UserParameterGet node."));

	GetSettings->PropertyGuid = Pd->ID;
	GetSettings->PropertyName = Pd->Name;
	// Rebuild the node's pins so the output pin is (re)labeled for the chosen parameter.
	GetNode->UpdateAfterSettingsChangeDuringCreation();

	FName OutLabel = NAME_None;
	const TArray<TObjectPtr<UPCGPin>>& OutPins = GetNode->GetOutputPins();
	if (OutPins.Num() > 0 && OutPins[0]) { OutLabel = OutPins[0]->Properties.Label; }

	UPCGNode* Edged = Graph->AddEdge(GetNode, OutLabel, Target, FName(*Property));
	Graph->MarkPackageDirty();

	if (!Edged)
		return MakeErrorJson(FString::Printf(TEXT("Created the parameter getter but failed to connect '%s' (out '%s') -> node '%s' pin '%s'. Check the override pin label."),
			*ParamName, *OutLabel.ToString(), *NodeName, *Property));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("getterNode"), GetNode->GetName());
	Result->SetStringField(TEXT("outputPin"), OutLabel.ToString());
	Result->SetStringField(TEXT("boundTo"), FString::Printf(TEXT("%s.%s"), *NodeName, *Property));
	return JsonToString(Result);
}
