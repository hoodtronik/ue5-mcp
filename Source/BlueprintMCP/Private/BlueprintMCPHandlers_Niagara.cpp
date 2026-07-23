#include "BlueprintMCPServer.h"

// CLAUDE-NOTE: Niagara asset + stack authoring handlers (Tier 1 + Tier 2 of the
// niagara_extension_plan). Tier 3 (custom UNiagaraNode graph authoring) lives
// in a future BlueprintMCPHandlers_NiagaraGraph.cpp if ever needed.
//
// Plugin is Type=Editor (BlueprintMCP.uplugin) so the whole TU is editor-only;
// no explicit WITH_EDITOR guards are needed for factories or the stack ViewModel,
// matching what BlueprintMCPHandlers_MaterialMutation.cpp does.

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitterFactoryNew.h"

// Tier 2: stack authoring
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraNode.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraParameterStore.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraParameterMapHistory.h"
#include "UObject/UnrealType.h"

// CLAUDE-NOTE: curve / dynamic-input support for set_module_input. Curve inputs
// (size/color/velocity over life) are authored by grafting a "*FromCurve" dynamic
// input onto the module-input override pin, then setting that dynamic input's Data
// Interface input to a curve DI whose FRichCurve channels we populate. All done at
// the graph/pin level (no Stack ViewModel) via the exported SetDynamicInputForFunctionInput
// + SetDataInterfaceValueForFunctionInput. UNiagaraNodeInput::GetDataInterface() is NOT
// exported (MinimalAPI), so the fast-path reads the DI via reflection on the private
// DataInterface UPROPERTY.
#include "NiagaraNodeInput.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "NiagaraDataInterfaceVectorCurve.h"
#include "NiagaraDataInterfaceVector4Curve.h"
#include "NiagaraDataInterfaceColorCurve.h"
#include "Curves/RichCurve.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"

// ============================================================
// Helpers
// ============================================================

namespace
{
	/** Convert ENiagaraSimTarget to a stable string for JSON. */
	FString SimTargetToString(ENiagaraSimTarget Target)
	{
		switch (Target)
		{
		case ENiagaraSimTarget::CPUSim:        return TEXT("CPU");
		case ENiagaraSimTarget::GPUComputeSim: return TEXT("GPU");
		default:                               return TEXT("Unknown");
		}
	}

	/** Find a Niagara System asset by short name or full /Game/... path. Returns nullptr if not found. */
	UNiagaraSystem* FindNiagaraSystemByNameOrPath(const FString& NameOrPath)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& Registry = ARM.Get();

		TArray<FAssetData> Candidates;
		Registry.GetAssetsByClass(UNiagaraSystem::StaticClass()->GetClassPathName(), Candidates, false);

		for (const FAssetData& Data : Candidates)
		{
			const FString PathName = Data.GetSoftObjectPath().ToString();
			const FString AssetName = Data.AssetName.ToString();
			if (PathName.Equals(NameOrPath, ESearchCase::IgnoreCase) ||
				AssetName.Equals(NameOrPath, ESearchCase::IgnoreCase) ||
				PathName.StartsWith(NameOrPath, ESearchCase::IgnoreCase))
			{
				return Cast<UNiagaraSystem>(Data.GetAsset());
			}
		}
		return nullptr;
	}

	/** Find a Niagara Emitter asset by short name or full /Game/... path. Returns nullptr if not found. */
	UNiagaraEmitter* FindNiagaraEmitterByNameOrPath(const FString& NameOrPath)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& Registry = ARM.Get();

		TArray<FAssetData> Candidates;
		Registry.GetAssetsByClass(UNiagaraEmitter::StaticClass()->GetClassPathName(), Candidates, false);

		for (const FAssetData& Data : Candidates)
		{
			const FString PathName = Data.GetSoftObjectPath().ToString();
			const FString AssetName = Data.AssetName.ToString();
			if (PathName.Equals(NameOrPath, ESearchCase::IgnoreCase) ||
				AssetName.Equals(NameOrPath, ESearchCase::IgnoreCase) ||
				PathName.StartsWith(NameOrPath, ESearchCase::IgnoreCase))
			{
				return Cast<UNiagaraEmitter>(Data.GetAsset());
			}
		}
		return nullptr;
	}

	// CLAUDE-NOTE: stage string -> ENiagaraScriptUsage. The four authorable per-emitter
	// stages. "SpawnRate" lives in EmitterUpdate, particle init in ParticleSpawn — the
	// caller must pick the stage where the target module actually lives.
	bool StageToScriptUsage(const FString& Stage, ENiagaraScriptUsage& OutUsage)
	{
		if (Stage.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase))  { OutUsage = ENiagaraScriptUsage::EmitterSpawnScript;  return true; }
		if (Stage.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase)) { OutUsage = ENiagaraScriptUsage::EmitterUpdateScript; return true; }
		if (Stage.Equals(TEXT("ParticleSpawn"), ESearchCase::IgnoreCase)) { OutUsage = ENiagaraScriptUsage::ParticleSpawnScript; return true; }
		if (Stage.Equals(TEXT("ParticleUpdate"), ESearchCase::IgnoreCase)){ OutUsage = ENiagaraScriptUsage::ParticleUpdateScript;return true; }
		return false;
	}

	/** Return the per-stage UNiagaraScript on an emitter's version data. */
	UNiagaraScript* GetStageScript(FVersionedNiagaraEmitterData* Data, ENiagaraScriptUsage Usage)
	{
		if (!Data) { return nullptr; }
		switch (Usage)
		{
		case ENiagaraScriptUsage::EmitterSpawnScript:  return Data->EmitterSpawnScriptProps.Script;
		case ENiagaraScriptUsage::EmitterUpdateScript: return Data->EmitterUpdateScriptProps.Script;
		case ENiagaraScriptUsage::ParticleSpawnScript: return Data->SpawnScriptProps.Script;
		case ENiagaraScriptUsage::ParticleUpdateScript:return Data->UpdateScriptProps.Script;
		default: return nullptr;
		}
	}

	/** Reach the emitter's shared UNiagaraGraph (spawn+update share one graph). */
	UNiagaraGraph* GetEmitterGraph(FVersionedNiagaraEmitterData* Data)
	{
		if (!Data) { return nullptr; }
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
		return Source ? Source->NodeGraph : nullptr;
	}

	/** Map a type string to a Niagara type def. Returns false for unsupported types. */
	bool ResolveNiagaraType(const FString& TypeStr, FNiagaraTypeDefinition& OutType)
	{
		if (TypeStr.Equals(TEXT("float"), ESearchCase::IgnoreCase))  { OutType = FNiagaraTypeDefinition::GetFloatDef();  return true; }
		if (TypeStr.Equals(TEXT("int"), ESearchCase::IgnoreCase))    { OutType = FNiagaraTypeDefinition::GetIntDef();    return true; }
		if (TypeStr.Equals(TEXT("bool"), ESearchCase::IgnoreCase))   { OutType = FNiagaraTypeDefinition::GetBoolDef();   return true; }
		if (TypeStr.Equals(TEXT("vec2"), ESearchCase::IgnoreCase))   { OutType = FNiagaraTypeDefinition::GetVec2Def();   return true; }
		if (TypeStr.Equals(TEXT("vec3"), ESearchCase::IgnoreCase) ||
			TypeStr.Equals(TEXT("vector"), ESearchCase::IgnoreCase)) { OutType = FNiagaraTypeDefinition::GetVec3Def();   return true; }
		if (TypeStr.Equals(TEXT("vec4"), ESearchCase::IgnoreCase))   { OutType = FNiagaraTypeDefinition::GetVec4Def();   return true; }
		if (TypeStr.Equals(TEXT("color"), ESearchCase::IgnoreCase) ||
			TypeStr.Equals(TEXT("linearcolor"), ESearchCase::IgnoreCase)) { OutType = FNiagaraTypeDefinition::GetColorDef(); return true; }
		return false;
	}

	// CLAUDE-NOTE: write a JSON value into an FNiagaraVariable's data block by type.
	// Niagara vectors are float-precision (FVector3f/4f), bool is FNiagaraBool. Handles
	// the common authorable types. Returns false + reason on type/shape mismatch.
	bool ApplyJsonValueToNiagaraVar(FNiagaraVariable& Var, const TSharedPtr<FJsonObject>& Json, FString& OutError)
	{
		const FNiagaraTypeDefinition Type = Var.GetType();
		Var.AllocateData();

		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			double V = 0.0;
			if (!Json->TryGetNumberField(TEXT("value"), V)) { OutError = TEXT("float type expects numeric 'value'"); return false; }
			Var.SetValue<float>(static_cast<float>(V));
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef())
		{
			int32 V = 0;
			if (!Json->TryGetNumberField(TEXT("value"), V)) { OutError = TEXT("int type expects integer 'value'"); return false; }
			Var.SetValue<int32>(V);
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			bool V = false;
			if (!Json->TryGetBoolField(TEXT("value"), V)) { OutError = TEXT("bool type expects boolean 'value'"); return false; }
			FNiagaraBool NB; NB.SetValue(V);
			Var.SetValue<FNiagaraBool>(NB);
			return true;
		}

		// Vector/color types read from a JSON array "value": [x,y,...]
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Json->TryGetArrayField(TEXT("value"), Arr))
		{
			OutError = TEXT("vector/color type expects 'value' as a numeric array");
			return false;
		}
		auto Comp = [&](int32 Idx) -> float
		{
			return (Arr->IsValidIndex(Idx)) ? static_cast<float>((*Arr)[Idx]->AsNumber()) : 0.0f;
		};

		if (Type == FNiagaraTypeDefinition::GetVec2Def())
		{
			Var.SetValue<FVector2f>(FVector2f(Comp(0), Comp(1)));
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetVec3Def())
		{
			Var.SetValue<FVector3f>(FVector3f(Comp(0), Comp(1), Comp(2)));
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetVec4Def())
		{
			Var.SetValue<FVector4f>(FVector4f(Comp(0), Comp(1), Comp(2), Comp(3)));
			return true;
		}
		if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			Var.SetValue<FLinearColor>(FLinearColor(Comp(0), Comp(1), Comp(2), Arr->IsValidIndex(3) ? Comp(3) : 1.0f));
			return true;
		}

		OutError = TEXT("unsupported Niagara type for value assignment");
		return false;
	}

	/** Create a renderer-properties object of the requested class on the emitter. */
	// CLAUDE-NOTE: must pass NAME_None — the factory emitter already owns a subobject
	// literally named "Renderer" (its default sprite renderer). Reusing that name with a
	// different class is a FATAL UObject collision ("Cannot replace existing object of a
	// different class"). NAME_None auto-generates a unique name; RF_Transactional matches
	// how NiagaraEditor's own AddRenderer creates renderer properties (undo support).
	UNiagaraRendererProperties* NewRendererByType(const FString& RendererType, UNiagaraEmitter* Owner)
	{
		if (RendererType.Equals(TEXT("Sprite"), ESearchCase::IgnoreCase))  return NewObject<UNiagaraSpriteRendererProperties>(Owner, NAME_None, RF_Transactional);
		if (RendererType.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase))    return NewObject<UNiagaraMeshRendererProperties>(Owner, NAME_None, RF_Transactional);
		if (RendererType.Equals(TEXT("Ribbon"), ESearchCase::IgnoreCase))  return NewObject<UNiagaraRibbonRendererProperties>(Owner, NAME_None, RF_Transactional);
		if (RendererType.Equals(TEXT("Light"), ESearchCase::IgnoreCase))   return NewObject<UNiagaraLightRendererProperties>(Owner, NAME_None, RF_Transactional);
		return nullptr;
	}

	// ============================================================
	// Curve / dynamic-input helpers (set_module_input valueMode="curve")
	// ============================================================

	/** One JSON curve key: a time plus 1-4 component values (float=1, vec=2/3/4, color=rgba). */
	struct FCurveKeyJson
	{
		float Time = 0.0f;
		TArray<float> Value;
	};

	/** Maps a module-input type string to its curve dynamic-input asset + curve DI class + component count. */
	struct FCurveTypeInfo
	{
		const TCHAR* DynInputPath = nullptr;   // the "*FromCurve" dynamic input UNiagaraScript
		UClass*      DIClass      = nullptr;    // the concrete curve data-interface class
		int32        NumComponents = 0;
	};

	bool ResolveCurveTypeInfo(const FString& TypeStr, FCurveTypeInfo& Out)
	{
		// CLAUDE-NOTE: asset paths verified present in UE 5.6 at
		// /Niagara/DynamicInputs/ValueFromCurve/. int/bool intentionally unsupported in
		// curve mode (no standard *FromCurve dynamic input for them).
		if (TypeStr.Equals(TEXT("float"), ESearchCase::IgnoreCase))
		{
			Out = { TEXT("/Niagara/DynamicInputs/ValueFromCurve/FloatFromCurve.FloatFromCurve"), UNiagaraDataInterfaceCurve::StaticClass(), 1 };
			return true;
		}
		if (TypeStr.Equals(TEXT("vec2"), ESearchCase::IgnoreCase))
		{
			Out = { TEXT("/Niagara/DynamicInputs/ValueFromCurve/Vector2DFromCurve.Vector2DFromCurve"), UNiagaraDataInterfaceVector2DCurve::StaticClass(), 2 };
			return true;
		}
		if (TypeStr.Equals(TEXT("vec3"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
		{
			Out = { TEXT("/Niagara/DynamicInputs/ValueFromCurve/VectorFromCurve.VectorFromCurve"), UNiagaraDataInterfaceVectorCurve::StaticClass(), 3 };
			return true;
		}
		if (TypeStr.Equals(TEXT("vec4"), ESearchCase::IgnoreCase))
		{
			Out = { TEXT("/Niagara/DynamicInputs/ValueFromCurve/Vector4FromCurve.Vector4FromCurve"), UNiagaraDataInterfaceVector4Curve::StaticClass(), 4 };
			return true;
		}
		if (TypeStr.Equals(TEXT("color"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("linearcolor"), ESearchCase::IgnoreCase))
		{
			Out = { TEXT("/Niagara/DynamicInputs/ValueFromCurve/ColorFromCurve.ColorFromCurve"), UNiagaraDataInterfaceColorCurve::StaticClass(), 4 };
			return true;
		}
		return false;
	}

	/** Read the "curveKeys" JSON array: [{ "time": t, "value": [c0, c1, ...] }, ...]. */
	bool ParseCurveKeysJson(const TSharedRef<FJsonObject>& Json, int32 NumComponents, TArray<FCurveKeyJson>& OutKeys, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Json->TryGetArrayField(TEXT("curveKeys"), Arr) || Arr->Num() == 0)
		{
			OutError = TEXT("curve mode expects a non-empty 'curveKeys' array of { time, value:[...] }");
			return false;
		}
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* KeyObj = nullptr;
			if (!(*Arr)[i]->TryGetObject(KeyObj))
			{
				OutError = FString::Printf(TEXT("curveKeys[%d] is not an object"), i);
				return false;
			}
			double T = 0.0;
			if (!(*KeyObj)->TryGetNumberField(TEXT("time"), T))
			{
				OutError = FString::Printf(TEXT("curveKeys[%d] missing numeric 'time'"), i);
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* ValArr = nullptr;
			if (!(*KeyObj)->TryGetArrayField(TEXT("value"), ValArr) || ValArr->Num() < NumComponents)
			{
				OutError = FString::Printf(TEXT("curveKeys[%d] 'value' must be a numeric array with >= %d components"), i, NumComponents);
				return false;
			}
			FCurveKeyJson Key;
			Key.Time = static_cast<float>(T);
			for (int32 c = 0; c < NumComponents; ++c)
			{
				Key.Value.Add(static_cast<float>((*ValArr)[c]->AsNumber()));
			}
			OutKeys.Add(MoveTemp(Key));
		}
		return true;
	}

	/** Fill one FRichCurve channel from the parsed keys, reading component index Comp from each key. */
	void FillRichCurve(FRichCurve& Curve, const TArray<FCurveKeyJson>& Keys, int32 Comp, ERichCurveInterpMode Interp)
	{
		Curve.Reset(); // replace, don't append — keeps the iterate-loop idempotent
		for (const FCurveKeyJson& Key : Keys)
		{
			const FKeyHandle Handle = Curve.AddKey(Key.Time, Key.Value.IsValidIndex(Comp) ? Key.Value[Comp] : 0.0f);
			Curve.SetKeyInterpMode(Handle, Interp);
			if (Interp == RCIM_Cubic)
			{
				Curve.SetKeyTangentMode(Handle, RCTM_Auto);
			}
		}
		if (Interp == RCIM_Cubic)
		{
			Curve.AutoSetTangents();
		}
	}

	/** Populate a curve DI's channels from the parsed keys, then rebuild its LUT. */
	bool PopulateCurveDI(UNiagaraDataInterface* DI, const FString& TypeStr, const TArray<FCurveKeyJson>& Keys, ERichCurveInterpMode Interp, FString& OutError)
	{
		if (UNiagaraDataInterfaceCurve* C = Cast<UNiagaraDataInterfaceCurve>(DI))
		{
			FillRichCurve(C->Curve, Keys, 0, Interp);
		}
		else if (UNiagaraDataInterfaceVector2DCurve* V2 = Cast<UNiagaraDataInterfaceVector2DCurve>(DI))
		{
			FillRichCurve(V2->XCurve, Keys, 0, Interp);
			FillRichCurve(V2->YCurve, Keys, 1, Interp);
		}
		else if (UNiagaraDataInterfaceVectorCurve* V3 = Cast<UNiagaraDataInterfaceVectorCurve>(DI))
		{
			FillRichCurve(V3->XCurve, Keys, 0, Interp);
			FillRichCurve(V3->YCurve, Keys, 1, Interp);
			FillRichCurve(V3->ZCurve, Keys, 2, Interp);
		}
		else if (UNiagaraDataInterfaceVector4Curve* V4 = Cast<UNiagaraDataInterfaceVector4Curve>(DI))
		{
			FillRichCurve(V4->XCurve, Keys, 0, Interp);
			FillRichCurve(V4->YCurve, Keys, 1, Interp);
			FillRichCurve(V4->ZCurve, Keys, 2, Interp);
			FillRichCurve(V4->WCurve, Keys, 3, Interp);
		}
		else if (UNiagaraDataInterfaceColorCurve* CC = Cast<UNiagaraDataInterfaceColorCurve>(DI))
		{
			FillRichCurve(CC->RedCurve,   Keys, 0, Interp);
			FillRichCurve(CC->GreenCurve, Keys, 1, Interp);
			FillRichCurve(CC->BlueCurve,  Keys, 2, Interp);
			FillRichCurve(CC->AlphaCurve, Keys, 3, Interp);
		}
		else
		{
			OutError = TEXT("data interface is not a recognized curve type");
			return false;
		}

		// CLAUDE-NOTE: UpdateLUT() is the exported (NIAGARA_API) lookup-table rebuild — the
		// runtime sampler reads the LUT, not the FRichCurve, so this is mandatory after edits.
		Cast<UNiagaraDataInterfaceCurveBase>(DI)->UpdateLUT();
		return true;
	}

	/** Read a UNiagaraNodeInput's data interface via reflection (GetDataInterface() is not exported). */
	UNiagaraDataInterface* GetDataInterfaceFromInputNode(UNiagaraNodeInput* InputNode)
	{
		if (!InputNode)
		{
			return nullptr;
		}
		// CLAUDE-NOTE: UNiagaraNodeInput is UCLASS(MinimalAPI) and the DataInterface member is
		// private with no exported accessor; reflect the UPROPERTY to avoid an LNK2019.
		FObjectProperty* Prop = FindFProperty<FObjectProperty>(UNiagaraNodeInput::StaticClass(), TEXT("DataInterface"));
		if (!Prop)
		{
			return nullptr;
		}
		return Cast<UNiagaraDataInterface>(Prop->GetObjectPropertyValue_InContainer(InputNode));
	}

	/** Walk upstream from a node through input pins, returning the first curve DI found (fast-path reuse). */
	UNiagaraDataInterfaceCurveBase* FindCurveDIUpstream(UEdGraphNode* StartNode)
	{
		if (!StartNode)
		{
			return nullptr;
		}
		TSet<UEdGraphNode*> Visited;
		TArray<UEdGraphNode*> Queue;
		Queue.Add(StartNode);
		Visited.Add(StartNode);
		int32 Guard = 0;
		while (Queue.Num() > 0 && Guard++ < 256)
		{
			UEdGraphNode* Node = Queue.Pop();
			if (UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(Node))
			{
				if (UNiagaraDataInterfaceCurveBase* CurveDI = Cast<UNiagaraDataInterfaceCurveBase>(GetDataInterfaceFromInputNode(InputNode)))
				{
					return CurveDI;
				}
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input)
				{
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (Linked && Linked->GetOwningNode() && !Visited.Contains(Linked->GetOwningNode()))
						{
							Visited.Add(Linked->GetOwningNode());
							Queue.Add(Linked->GetOwningNode());
						}
					}
				}
			}
		}
		return nullptr;
	}
}

// ============================================================
// HandleCreateNiagaraSystem — create a new UNiagaraSystem asset
// ============================================================

FString FBlueprintMCPServer::HandleCreateNiagaraSystem(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString Name = Json->GetStringField(TEXT("name"));
	FString PackagePath = Json->GetStringField(TEXT("packagePath"));

	if (Name.IsEmpty() || PackagePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required fields: name, packagePath"));
	}
	if (!PackagePath.StartsWith(TEXT("/Game")))
	{
		return MakeErrorJson(TEXT("packagePath must start with '/Game'"));
	}

	// Refuse to clobber an existing asset.
	const FString FullPath = PackagePath / Name;
	if (FindNiagaraSystemByNameOrPath(FullPath))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("NiagaraSystem '%s' already exists at '%s'. Use a different name or delete the existing asset first."),
			*Name, *PackagePath));
	}

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: Creating NiagaraSystem '%s' in '%s'"), *Name, *PackagePath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UNiagaraSystem::StaticClass(), Factory);
	if (!NewAsset)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to create NiagaraSystem '%s' in '%s'"), *Name, *PackagePath));
	}

	UNiagaraSystem* System = Cast<UNiagaraSystem>(NewAsset);
	if (!System)
	{
		return MakeErrorJson(TEXT("Created asset is not a UNiagaraSystem"));
	}

	// Save the package so the asset survives editor restart.
	const bool bSaved = SaveGenericPackage(System);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetStringField(TEXT("assetPath"), System->GetPathName());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleCreateNiagaraEmitter — create a new UNiagaraEmitter asset
// ============================================================

FString FBlueprintMCPServer::HandleCreateNiagaraEmitter(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString Name = Json->GetStringField(TEXT("name"));
	FString PackagePath = Json->GetStringField(TEXT("packagePath"));

	if (Name.IsEmpty() || PackagePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required fields: name, packagePath"));
	}
	if (!PackagePath.StartsWith(TEXT("/Game")))
	{
		return MakeErrorJson(TEXT("packagePath must start with '/Game'"));
	}

	const FString FullPath = PackagePath / Name;
	if (FindNiagaraEmitterByNameOrPath(FullPath))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("NiagaraEmitter '%s' already exists at '%s'."), *Name, *PackagePath));
	}

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: Creating NiagaraEmitter '%s' in '%s'"), *Name, *PackagePath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UNiagaraEmitterFactoryNew* Factory = NewObject<UNiagaraEmitterFactoryNew>();

	// CLAUDE-NOTE: UNiagaraEmitterFactoryNew defaults bAddDefaultModulesAndRenderersToEmptyEmitter=true,
	// which seeds a full working emitter (EmitterState, SpawnRate=10, SystemLocation, AddVelocity,
	// sprite size/lifetime, Color, SolveForcesAndVelocity, + a Sprite Renderer) — it already emits.
	// Pass bare=true to author a truly empty emitter and build it up via Tier 2 tools instead.
	bool bBare = false;
	if (Json->TryGetBoolField(TEXT("bare"), bBare) && bBare)
	{
		Factory->bAddDefaultModulesAndRenderersToEmptyEmitter = false;
	}

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UNiagaraEmitter::StaticClass(), Factory);
	if (!NewAsset)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to create NiagaraEmitter '%s' in '%s'"), *Name, *PackagePath));
	}

	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(NewAsset);
	if (!Emitter)
	{
		return MakeErrorJson(TEXT("Created asset is not a UNiagaraEmitter"));
	}

	// Optional sim target hint at creation time. Defaults to CPU.
	FString SimTargetStr;
	if (Json->TryGetStringField(TEXT("simTarget"), SimTargetStr))
	{
		if (FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData())
		{
			if (SimTargetStr.Equals(TEXT("GPU"), ESearchCase::IgnoreCase))
			{
				Data->SimTarget = ENiagaraSimTarget::GPUComputeSim;
			}
			else
			{
				Data->SimTarget = ENiagaraSimTarget::CPUSim;
			}
		}
	}

	const bool bSaved = SaveGenericPackage(Emitter);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetStringField(TEXT("assetPath"), Emitter->GetPathName());
	Result->SetBoolField(TEXT("bare"), bBare);
	if (const FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData())
	{
		Result->SetStringField(TEXT("simTarget"), SimTargetToString(Data->SimTarget));
	}
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleAddEmitterToSystem — attach an existing emitter asset as a handle on a system
// ============================================================

FString FBlueprintMCPServer::HandleAddEmitterToSystem(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SystemNameOrPath, EmitterNameOrPath, EmitterHandleName;
	if (!Json->TryGetStringField(TEXT("system"), SystemNameOrPath) || SystemNameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: system (name or /Game/... path)"));
	}
	if (!Json->TryGetStringField(TEXT("emitter"), EmitterNameOrPath) || EmitterNameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter (name or /Game/... path)"));
	}
	Json->TryGetStringField(TEXT("handleName"), EmitterHandleName);

	UNiagaraSystem* System = FindNiagaraSystemByNameOrPath(SystemNameOrPath);
	if (!System)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraSystem '%s' not found"), *SystemNameOrPath));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(EmitterNameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *EmitterNameOrPath));
	}

	// Default handle name = emitter asset name without the "NE_" prefix if present, else asset name.
	if (EmitterHandleName.IsEmpty())
	{
		EmitterHandleName = Emitter->GetName();
		if (EmitterHandleName.StartsWith(TEXT("NE_")))
		{
			EmitterHandleName = EmitterHandleName.RightChop(3);
		}
	}

	// Use the emitter's exposed version. This is the version users see in the editor
	// and the one we want our handle bound to.
	const FGuid ExposedVersion = Emitter->GetExposedVersion().VersionGuid;

	System->Modify();
	const FNiagaraEmitterHandle NewHandle = System->AddEmitterHandle(*Emitter, *EmitterHandleName, ExposedVersion);

	// Trigger a recompile so the system picks up the new emitter's scripts.
	System->RequestCompile(false);

	const bool bSaved = SaveGenericPackage(System);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system"), System->GetPathName());
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetStringField(TEXT("handleId"), NewHandle.GetId().ToString());
	Result->SetStringField(TEXT("handleName"), NewHandle.GetName().ToString());
	Result->SetNumberField(TEXT("emitterCount"), System->GetEmitterHandles().Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleListNiagaraSystems — list all NiagaraSystem assets in the project
// ============================================================

FString FBlueprintMCPServer::HandleListNiagaraSystems(const TMap<FString, FString>& Params)
{
	FString PathFilter;
	if (const FString* Found = Params.Find(TEXT("path")))
	{
		PathFilter = *Found;
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = ARM.Get();

	TArray<FAssetData> SystemAssets;
	Registry.GetAssetsByClass(UNiagaraSystem::StaticClass()->GetClassPathName(), SystemAssets, false);

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FAssetData& Data : SystemAssets)
	{
		const FString PathName = Data.GetSoftObjectPath().ToString();
		if (!PathFilter.IsEmpty() && !PathName.StartsWith(PathFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), Data.AssetName.ToString());
		Item->SetStringField(TEXT("path"), PathName);
		Item->SetStringField(TEXT("packagePath"), Data.PackagePath.ToString());
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), Items.Num());
	Result->SetArrayField(TEXT("systems"), Items);
	if (!PathFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("pathFilter"), PathFilter);
	}
	return JsonToString(Result);
}

// ============================================================
// HandleGetNiagaraSystemSummary — structured info about a system
// ============================================================

FString FBlueprintMCPServer::HandleGetNiagaraSystemSummary(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath;
	if (!Json->TryGetStringField(TEXT("system"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: system"));
	}

	UNiagaraSystem* System = FindNiagaraSystemByNameOrPath(NameOrPath);
	if (!System)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraSystem '%s' not found"), *NameOrPath));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), System->GetPathName());
	Result->SetStringField(TEXT("name"), System->GetName());

	// Emitter handles
	TArray<TSharedPtr<FJsonValue>> EmitterArr;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		TSharedRef<FJsonObject> H = MakeShared<FJsonObject>();
		H->SetStringField(TEXT("handleId"), Handle.GetId().ToString());
		H->SetStringField(TEXT("handleName"), Handle.GetName().ToString());
		H->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());

		const FVersionedNiagaraEmitter VE = Handle.GetInstance();
		if (VE.Emitter)
		{
			H->SetStringField(TEXT("emitterAsset"), VE.Emitter->GetPathName());
		}
		EmitterArr.Add(MakeShared<FJsonValueObject>(H));
	}
	Result->SetArrayField(TEXT("emitters"), EmitterArr);
	Result->SetNumberField(TEXT("emitterCount"), EmitterArr.Num());

	// User parameters (the uds_* RenderStream-exposed values live here)
	TArray<TSharedPtr<FJsonValue>> UserParamArr;
	TArray<FNiagaraVariable> UserParams;
	System->GetExposedParameters().GetParameters(UserParams);
	for (const FNiagaraVariable& Var : UserParams)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Var.GetName().ToString());
		P->SetStringField(TEXT("type"), Var.GetType().GetName());
		UserParamArr.Add(MakeShared<FJsonValueObject>(P));
	}
	Result->SetArrayField(TEXT("userParameters"), UserParamArr);
	Result->SetNumberField(TEXT("userParameterCount"), UserParamArr.Num());

	// Fixed bounds (used by RenderStream / disguise sometimes for culling)
	const FBox Bounds = System->GetFixedBounds();
	if (Bounds.IsValid)
	{
		TSharedRef<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetNumberField(TEXT("minX"), Bounds.Min.X);
		BoundsObj->SetNumberField(TEXT("minY"), Bounds.Min.Y);
		BoundsObj->SetNumberField(TEXT("minZ"), Bounds.Min.Z);
		BoundsObj->SetNumberField(TEXT("maxX"), Bounds.Max.X);
		BoundsObj->SetNumberField(TEXT("maxY"), Bounds.Max.Y);
		BoundsObj->SetNumberField(TEXT("maxZ"), Bounds.Max.Z);
		Result->SetObjectField(TEXT("fixedBounds"), BoundsObj);
	}

	return JsonToString(Result);
}

// ============================================================
// HandleGetNiagaraEmitterSummary — structured info about an emitter asset
// ============================================================

FString FBlueprintMCPServer::HandleGetNiagaraEmitterSummary(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), Emitter->GetPathName());
	Result->SetStringField(TEXT("name"), Emitter->GetName());
	Result->SetStringField(TEXT("exposedVersion"), Emitter->GetExposedVersion().VersionGuid.ToString());

	const FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	if (!Data)
	{
		Result->SetStringField(TEXT("warning"), TEXT("Emitter has no latest version data — newly created or corrupt."));
		return JsonToString(Result);
	}

	Result->SetStringField(TEXT("simTarget"), SimTargetToString(Data->SimTarget));

	// Renderers (Tier 2 will allow add/remove; Tier 1 just reports them).
	TArray<TSharedPtr<FJsonValue>> RendererArr;
	for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
	{
		if (!Renderer) { continue; }
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
		R->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
		RendererArr.Add(MakeShared<FJsonValueObject>(R));
	}
	Result->SetArrayField(TEXT("renderers"), RendererArr);
	Result->SetNumberField(TEXT("rendererCount"), RendererArr.Num());

	// Script-stage presence (Tier 2 lands modules onto these stages).
	TSharedRef<FJsonObject> Stages = MakeShared<FJsonObject>();
	Stages->SetBoolField(TEXT("emitterSpawn"),  Data->EmitterSpawnScriptProps.Script != nullptr);
	Stages->SetBoolField(TEXT("emitterUpdate"), Data->EmitterUpdateScriptProps.Script != nullptr);
	Stages->SetBoolField(TEXT("particleSpawn"), Data->SpawnScriptProps.Script != nullptr);
	Stages->SetBoolField(TEXT("particleUpdate"), Data->UpdateScriptProps.Script != nullptr);
	Result->SetObjectField(TEXT("stages"), Stages);

	return JsonToString(Result);
}

// ============================================================
// TIER 2 — stack authoring
// ============================================================

// CLAUDE-NOTE: Tier 2 mutates an emitter ASSET in place (graph/scripts/renderers
// live on the emitter, not the system). After structural changes we MarkPackageDirty
// and call UNiagaraSystem::RequestCompileForEmitter so any system already referencing
// the emitter recompiles. The change persists on save regardless of compile timing.
namespace
{
	/** Build the FVersionedNiagaraEmitter for the emitter's exposed version + kick a recompile. */
	void RequestEmitterRecompile(UNiagaraEmitter* Emitter)
	{
		if (!Emitter) { return; }
		const FVersionedNiagaraEmitter Versioned(Emitter, Emitter->GetExposedVersion().VersionGuid);
		UNiagaraSystem::RequestCompileForEmitter(Versioned);
	}

	// CLAUDE-NOTE: UNiagaraGraph::FindOutputNode is not DLL-exported, so we find the
	// stage's output node header-only via GetNodesOfClass + the inline GetUsage() accessor.
	UNiagaraNodeOutput* FindStageOutputNode(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage)
	{
		if (!Graph) { return nullptr; }
		TArray<UNiagaraNodeOutput*> OutputNodes;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		for (UNiagaraNodeOutput* Node : OutputNodes)
		{
			if (Node && UNiagaraScript::IsEquivalentUsage(Node->GetUsage(), Usage)) { return Node; }
		}
		return nullptr;
	}

	/** Find a module function-call node by its NodeGuid within an emitter graph. */
	UNiagaraNodeFunctionCall* FindModuleNodeByGuid(UNiagaraGraph* Graph, const FGuid& NodeGuid)
	{
		if (!Graph || !NodeGuid.IsValid()) { return nullptr; }
		TArray<UNiagaraNodeFunctionCall*> FunctionNodes;
		Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(FunctionNodes);
		for (UNiagaraNodeFunctionCall* Node : FunctionNodes)
		{
			if (Node && Node->NodeGuid == NodeGuid) { return Node; }
		}
		return nullptr;
	}

	// CLAUDE-NOTE: header-only reimplementation of the non-exported
	// FNiagaraStackGraphUtilities::GetParameterMapInputPin — the input pin whose Niagara
	// type is the ParameterMap def. PinToTypeDefinition is exported; we iterate Node->Pins
	// directly (public UEdGraphNode member) instead of the non-exported GetInputPins.
	UEdGraphPin* FindParameterMapInputPin(UNiagaraNode* Node)
	{
		if (!Node) { return nullptr; }
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input &&
				UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				return Pin;
			}
		}
		return nullptr;
	}

	// CLAUDE-NOTE: header-only reimplementation of the non-exported
	// FNiagaraStackGraphUtilities::GetOrderedModuleNodes. Walks the parameter-map chain
	// backward from a stage's output node; each UNiagaraNodeFunctionCall in the chain is a
	// stack module, collected in execution order (insert-at-front while walking back).
	void GetOrderedStageModules(UNiagaraNodeOutput* OutputNode, TArray<UNiagaraNodeFunctionCall*>& OutModules)
	{
		UNiagaraNode* PreviousNode = OutputNode;
		while (PreviousNode != nullptr)
		{
			UEdGraphPin* InputPin = FindParameterMapInputPin(PreviousNode);
			if (InputPin != nullptr && InputPin->LinkedTo.Num() == 1)
			{
				UNiagaraNode* CurrentNode = Cast<UNiagaraNode>(InputPin->LinkedTo[0]->GetOwningNode());
				if (UNiagaraNodeFunctionCall* ModuleNode = Cast<UNiagaraNodeFunctionCall>(CurrentNode))
				{
					OutModules.Insert(ModuleNode, 0);
				}
				PreviousNode = CurrentNode;
			}
			else
			{
				PreviousNode = nullptr;
			}
		}
	}

	// CLAUDE-NOTE: reverse of ResolveNiagaraType — map a Niagara type to the string the
	// set_module_input 'type' enum accepts. Returns empty for types set_module_input can't
	// write yet, so list_module_inputs can flag which inputs are settable.
	FString NiagaraTypeToInputTypeString(const FNiagaraTypeDefinition& T)
	{
		if (T == FNiagaraTypeDefinition::GetFloatDef()) { return TEXT("float"); }
		if (T == FNiagaraTypeDefinition::GetIntDef())   { return TEXT("int"); }
		if (T == FNiagaraTypeDefinition::GetBoolDef())  { return TEXT("bool"); }
		if (T == FNiagaraTypeDefinition::GetVec2Def())  { return TEXT("vec2"); }
		if (T == FNiagaraTypeDefinition::GetVec3Def())  { return TEXT("vec3"); }
		if (T == FNiagaraTypeDefinition::GetVec4Def())  { return TEXT("vec4"); }
		if (T == FNiagaraTypeDefinition::GetColorDef()) { return TEXT("color"); }
		return FString();
	}
}

// ============================================================
// HandleSetEmitterSimTarget — CPU vs GPU compute
// ============================================================

FString FBlueprintMCPServer::HandleSetEmitterSimTarget(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath, SimTargetStr;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}
	if (!Json->TryGetStringField(TEXT("simTarget"), SimTargetStr) || SimTargetStr.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: simTarget (CPU|GPU)"));
	}

	ENiagaraSimTarget NewTarget;
	if (SimTargetStr.Equals(TEXT("CPU"), ESearchCase::IgnoreCase))      { NewTarget = ENiagaraSimTarget::CPUSim; }
	else if (SimTargetStr.Equals(TEXT("GPU"), ESearchCase::IgnoreCase)) { NewTarget = ENiagaraSimTarget::GPUComputeSim; }
	else { return MakeErrorJson(TEXT("simTarget must be 'CPU' or 'GPU'")); }

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	if (!Data)
	{
		return MakeErrorJson(TEXT("Emitter has no latest version data"));
	}

	Emitter->Modify();
	Data->SimTarget = NewTarget;
	Emitter->MarkPackageDirty();
	RequestEmitterRecompile(Emitter);
	const bool bSaved = SaveGenericPackage(Emitter);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetStringField(TEXT("simTarget"), SimTargetToString(Data->SimTarget));
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleAddNiagaraRenderer — add a renderer (Sprite/Mesh/Ribbon/Light)
// ============================================================

FString FBlueprintMCPServer::HandleAddNiagaraRenderer(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath, RendererType;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}
	if (!Json->TryGetStringField(TEXT("rendererType"), RendererType) || RendererType.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: rendererType (Sprite|Mesh|Ribbon|Light)"));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	if (!Data)
	{
		return MakeErrorJson(TEXT("Emitter has no latest version data"));
	}

	UNiagaraRendererProperties* Renderer = NewRendererByType(RendererType, Emitter);
	if (!Renderer)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unknown rendererType '%s'. Use Sprite, Mesh, Ribbon, or Light."), *RendererType));
	}

	Emitter->Modify();
	Emitter->AddRenderer(Renderer, Emitter->GetExposedVersion().VersionGuid);
	Emitter->MarkPackageDirty();
	RequestEmitterRecompile(Emitter);
	const bool bSaved = SaveGenericPackage(Emitter);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetStringField(TEXT("rendererClass"), Renderer->GetClass()->GetName());
	Result->SetNumberField(TEXT("rendererCount"), Data->GetRenderers().Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleAddUserParameter — add a User Parameter to a system
// ============================================================

FString FBlueprintMCPServer::HandleAddUserParameter(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SystemNameOrPath, ParamName, TypeStr;
	if (!Json->TryGetStringField(TEXT("system"), SystemNameOrPath) || SystemNameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: system"));
	}
	if (!Json->TryGetStringField(TEXT("name"), ParamName) || ParamName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: name"));
	}
	if (!Json->TryGetStringField(TEXT("type"), TypeStr) || TypeStr.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: type (float|int|bool|vec2|vec3|vec4|color)"));
	}

	FNiagaraTypeDefinition TypeDef;
	if (!ResolveNiagaraType(TypeStr, TypeDef))
	{
		return MakeErrorJson(FString::Printf(TEXT("Unsupported type '%s'"), *TypeStr));
	}

	UNiagaraSystem* System = FindNiagaraSystemByNameOrPath(SystemNameOrPath);
	if (!System)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraSystem '%s' not found"), *SystemNameOrPath));
	}

	// CLAUDE-NOTE: user parameters live under the "User." namespace; the redirection
	// store expects the prefixed name. Add it if the caller omitted it.
	FString FullName = ParamName.StartsWith(TEXT("User.")) ? ParamName : (TEXT("User.") + ParamName);
	const FNiagaraVariable NewVar(TypeDef, FName(*FullName));

	FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
	TArray<FNiagaraVariable> Existing;
	Store.GetParameters(Existing);
	if (Existing.Contains(NewVar))
	{
		return MakeErrorJson(FString::Printf(TEXT("User parameter '%s' already exists"), *FullName));
	}

	System->Modify();
	const bool bAdded = Store.AddParameter(NewVar);
	System->MarkPackageDirty();
	const bool bSaved = SaveGenericPackage(System);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system"), System->GetPathName());
	Result->SetStringField(TEXT("name"), FullName);
	Result->SetStringField(TEXT("type"), TypeDef.GetName());
	Result->SetBoolField(TEXT("added"), bAdded);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleSetUserParameterDefault — set a User Parameter's default value
// ============================================================

FString FBlueprintMCPServer::HandleSetUserParameterDefault(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SystemNameOrPath, ParamName;
	if (!Json->TryGetStringField(TEXT("system"), SystemNameOrPath) || SystemNameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: system"));
	}
	if (!Json->TryGetStringField(TEXT("name"), ParamName) || ParamName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: name"));
	}

	UNiagaraSystem* System = FindNiagaraSystemByNameOrPath(SystemNameOrPath);
	if (!System)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraSystem '%s' not found"), *SystemNameOrPath));
	}

	const FString FullName = ParamName.StartsWith(TEXT("User.")) ? ParamName : (TEXT("User.") + ParamName);

	FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
	TArray<FNiagaraVariable> Existing;
	Store.GetParameters(Existing);

	FNiagaraVariable Target;
	for (const FNiagaraVariable& Var : Existing)
	{
		if (Var.GetName().ToString().Equals(FullName, ESearchCase::IgnoreCase))
		{
			Target = Var;
			break;
		}
	}
	if (!Target.IsValid())
	{
		return MakeErrorJson(FString::Printf(
			TEXT("User parameter '%s' not found. Add it with add_user_parameter first."), *FullName));
	}

	FString ApplyError;
	if (!ApplyJsonValueToNiagaraVar(Target, Json.ToSharedRef(), ApplyError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to apply value: %s"), *ApplyError));
	}

	System->Modify();
	// bAdd=false: the parameter already exists; just overwrite its data block.
	Store.SetParameterData(Target.GetData(), Target, /*bAdd*/ false);
	System->MarkPackageDirty();
	const bool bSaved = SaveGenericPackage(System);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system"), System->GetPathName());
	Result->SetStringField(TEXT("name"), FullName);
	Result->SetStringField(TEXT("type"), Target.GetType().GetName());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleAddNiagaraModule — add a module script to a stack stage
// ============================================================

FString FBlueprintMCPServer::HandleAddNiagaraModule(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath, Stage, ModuleScriptPath;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}
	if (!Json->TryGetStringField(TEXT("stage"), Stage) || Stage.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: stage (EmitterSpawn|EmitterUpdate|ParticleSpawn|ParticleUpdate)"));
	}
	if (!Json->TryGetStringField(TEXT("moduleScript"), ModuleScriptPath) || ModuleScriptPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: moduleScript (asset path of a UNiagaraScript module)"));
	}

	ENiagaraScriptUsage Usage;
	if (!StageToScriptUsage(Stage, Usage))
	{
		return MakeErrorJson(FString::Printf(TEXT("Unknown stage '%s'"), *Stage));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	UNiagaraGraph* Graph = GetEmitterGraph(Data);
	if (!Graph)
	{
		return MakeErrorJson(TEXT("Could not resolve emitter node graph"));
	}

	UNiagaraNodeOutput* OutputNode = FindStageOutputNode(Graph, Usage);
	if (!OutputNode)
	{
		return MakeErrorJson(FString::Printf(TEXT("Stage '%s' has no output node on this emitter"), *Stage));
	}

	UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, *ModuleScriptPath);
	if (!ModuleScript)
	{
		return MakeErrorJson(FString::Printf(TEXT("Could not load module script '%s'"), *ModuleScriptPath));
	}

	int32 TargetIndex = INDEX_NONE;
	int32 IndexTmp = 0;
	if (Json->TryGetNumberField(TEXT("index"), IndexTmp)) { TargetIndex = IndexTmp; }

	Emitter->Modify();
	Graph->Modify();
	UNiagaraNodeFunctionCall* ModuleNode =
		FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModuleScript, *OutputNode, TargetIndex);
	if (!ModuleNode)
	{
		return MakeErrorJson(TEXT("AddScriptModuleToStack returned null — module may be invalid for this stage"));
	}

	Emitter->MarkPackageDirty();
	RequestEmitterRecompile(Emitter);
	const bool bSaved = SaveGenericPackage(Emitter);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetStringField(TEXT("stage"), Stage);
	Result->SetStringField(TEXT("moduleScript"), ModuleScript->GetPathName());
	Result->SetStringField(TEXT("moduleName"), ModuleNode->GetFunctionName());
	// CLAUDE-NOTE: return the node GUID so set_module_input can target this exact module.
	Result->SetStringField(TEXT("moduleNodeGuid"), ModuleNode->NodeGuid.ToString());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleSetModuleInput — set a module input to an inline constant
// ============================================================

FString FBlueprintMCPServer::HandleSetModuleInput(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath, Stage, ModuleGuidStr, InputName, TypeStr;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}
	if (!Json->TryGetStringField(TEXT("stage"), Stage) || Stage.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: stage"));
	}
	if (!Json->TryGetStringField(TEXT("moduleNodeGuid"), ModuleGuidStr) || ModuleGuidStr.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: moduleNodeGuid (from add_niagara_module)"));
	}
	if (!Json->TryGetStringField(TEXT("input"), InputName) || InputName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: input (module input name, e.g. 'SpawnRate')"));
	}
	if (!Json->TryGetStringField(TEXT("type"), TypeStr) || TypeStr.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: type (float|int|vec2|vec3|vec4|color)"));
	}

	// CLAUDE-NOTE: every supported type (incl. bool) is written via the override-pin
	// default-value path below — the same branch UNiagaraStackFunctionInput::SetLocalValue
	// uses for non-rapid-iteration inputs. bool serializes fine through
	// TryGetPinDefaultValueFromNiagaraVariable, so no type is excluded here.
	FNiagaraTypeDefinition InputType;
	if (!ResolveNiagaraType(TypeStr, InputType))
	{
		return MakeErrorJson(FString::Printf(TEXT("Unsupported type '%s'"), *TypeStr));
	}

	FGuid ModuleGuid;
	if (!FGuid::Parse(ModuleGuidStr, ModuleGuid))
	{
		return MakeErrorJson(TEXT("moduleNodeGuid is not a valid GUID"));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	UNiagaraGraph* Graph = GetEmitterGraph(Data);
	if (!Graph)
	{
		return MakeErrorJson(TEXT("Could not resolve emitter node graph"));
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNodeByGuid(Graph, ModuleGuid);
	if (!ModuleNode)
	{
		return MakeErrorJson(FString::Printf(TEXT("No module node with GUID '%s' in this emitter"), *ModuleGuidStr));
	}

	// CLAUDE-NOTE: validate the input exists on this module and the type matches BEFORE
	// touching the graph. Without this, GetOrCreateStackFunctionInputOverridePin happily
	// creates an override for a non-existent input (junk that silently does nothing). Snap
	// InputName to the module's exact spelling/case so the aliased handle resolves correctly.
	{
		TArray<FNiagaraVariable> ModuleInputs;
		FCompileConstantResolver Resolver;
		FNiagaraStackGraphUtilities::GetStackFunctionInputs(
			*ModuleNode, ModuleInputs, Resolver,
			FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly,
			/*bIgnoreDisabled*/ false);

		const FNiagaraVariable* MatchedInput = nullptr;
		for (const FNiagaraVariable& Var : ModuleInputs)
		{
			FString BareName = Var.GetName().ToString();
			if (BareName.StartsWith(TEXT("Module."))) { BareName = BareName.RightChop(7); }
			if (BareName.Equals(InputName, ESearchCase::IgnoreCase))
			{
				MatchedInput = &Var;
				InputName = BareName; // exact spelling/case for the handle below
				break;
			}
		}
		if (!MatchedInput)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Input '%s' not found on module '%s'. Use list_module_inputs to see valid input names."),
				*InputName, *ModuleNode->GetFunctionName()));
		}
		if (MatchedInput->GetType() != InputType)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Input '%s' on module '%s' is type '%s', not '%s'. Use list_module_inputs to check types."),
				*InputName, *ModuleNode->GetFunctionName(), *MatchedInput->GetType().GetName(), *InputType.GetName()));
		}
	}

	// ---- curve / dynamic-input mode ------------------------------------------
	// CLAUDE-NOTE: valueMode="curve" grafts a *FromCurve dynamic input onto the input's
	// override pin and writes an FRichCurve into its data interface (size/color/velocity
	// over life). Re-setting the SAME curve input reuses the existing graft (fast path) so
	// the iterate-on-screenshot loop stays idempotent. Switching an input from a different
	// override type to a curve is not yet supported (the engine's teardown helper isn't
	// exported) — we return a clean error rather than tripping the setters' LinkedTo==0 check.
	FString ValueMode = TEXT("constant");
	Json->TryGetStringField(TEXT("valueMode"), ValueMode);
	if (ValueMode.Equals(TEXT("curve"), ESearchCase::IgnoreCase))
	{
		FCurveTypeInfo CurveInfo;
		if (!ResolveCurveTypeInfo(TypeStr, CurveInfo))
		{
			return MakeErrorJson(FString::Printf(TEXT("curve mode supports float/vec2/vec3/vec4/color, not '%s'"), *TypeStr));
		}

		TArray<FCurveKeyJson> Keys;
		FString KeyError;
		if (!ParseCurveKeysJson(Json.ToSharedRef(), CurveInfo.NumComponents, Keys, KeyError))
		{
			return MakeErrorJson(KeyError);
		}

		ERichCurveInterpMode Interp = RCIM_Cubic; // smooth default suits organic looks
		FString InterpStr;
		if (Json->TryGetStringField(TEXT("curveInterp"), InterpStr))
		{
			if (InterpStr.Equals(TEXT("linear"), ESearchCase::IgnoreCase))        { Interp = RCIM_Linear; }
			else if (InterpStr.Equals(TEXT("constant"), ESearchCase::IgnoreCase)) { Interp = RCIM_Constant; }
			else if (InterpStr.Equals(TEXT("cubic"), ESearchCase::IgnoreCase))    { Interp = RCIM_Cubic; }
			else { return MakeErrorJson(TEXT("curveInterp must be cubic|linear|constant")); }
		}

		const FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*InputName));
		const FNiagaraParameterHandle AliasedHandle =
			FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);

		Emitter->Modify();
		Graph->Modify();

		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

		bool bReused = false;
		if (OverridePin.LinkedTo.Num() > 0)
		{
			// Existing graft — reuse only if it's a *FromCurve dynamic input feeding a curve DI.
			UEdGraphNode* LinkedNode = OverridePin.LinkedTo[0]->GetOwningNode();
			UNiagaraDataInterfaceCurveBase* ExistingDI =
				Cast<UNiagaraNodeFunctionCall>(LinkedNode) ? FindCurveDIUpstream(LinkedNode) : nullptr;
			if (!ExistingDI)
			{
				return MakeErrorJson(TEXT("This input already has a non-curve override. Clear it in the Niagara editor before setting a curve (switching override types via MCP is not yet supported)."));
			}
			ExistingDI->Modify();
			FString PopError;
			if (!PopulateCurveDI(ExistingDI, TypeStr, Keys, Interp, PopError))
			{
				return MakeErrorJson(PopError);
			}
			bReused = true;
		}
		else
		{
			// Fresh graft: load the *FromCurve dynamic input and splice it onto the override pin.
			UNiagaraScript* DynScript = LoadObject<UNiagaraScript>(nullptr, CurveInfo.DynInputPath);
			if (!DynScript)
			{
				return MakeErrorJson(FString::Printf(TEXT("Could not load curve dynamic input '%s'"), CurveInfo.DynInputPath));
			}
			UNiagaraNodeFunctionCall* DynNode = nullptr;
			FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(OverridePin, DynScript, DynNode);
			if (!DynNode)
			{
				return MakeErrorJson(TEXT("Failed to graft the curve dynamic input onto the override pin"));
			}

			// Discover the dynamic input's data-interface (curve) input by type, then create the curve DI on it.
			TArray<FNiagaraVariable> DynInputs;
			FCompileConstantResolver DynResolver;
			FNiagaraStackGraphUtilities::GetStackFunctionInputs(
				*DynNode, DynInputs, DynResolver,
				FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly,
				/*bIgnoreDisabled*/ false);

			const FNiagaraVariable* DIInput = nullptr;
			for (const FNiagaraVariable& Var : DynInputs)
			{
				if (Var.GetType().IsDataInterface() && Var.GetType().GetClass() &&
					Var.GetType().GetClass()->IsChildOf(UNiagaraDataInterfaceCurveBase::StaticClass()))
				{
					DIInput = &Var;
					break;
				}
			}
			if (!DIInput)
			{
				return MakeErrorJson(TEXT("Curve dynamic input has no curve data-interface input (unexpected asset layout)"));
			}

			FString DIInputName = DIInput->GetName().ToString();
			if (DIInputName.StartsWith(TEXT("Module."))) { DIInputName = DIInputName.RightChop(7); }
			const FNiagaraParameterHandle DIHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*DIInputName));
			const FNiagaraParameterHandle DIAliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(DIHandle, DynNode);
			const FNiagaraTypeDefinition DIType(CurveInfo.DIClass);

			UEdGraphPin& DIOverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				*DynNode, DIAliased, DIType, FGuid(), FGuid());

			UNiagaraDataInterface* OutDI = nullptr;
			FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
				DIOverridePin, CurveInfo.DIClass, DIAliased.GetParameterHandleString().ToString(), OutDI);
			if (!OutDI)
			{
				return MakeErrorJson(TEXT("Failed to create the curve data interface on the dynamic input"));
			}
			OutDI->Modify();
			FString PopError;
			if (!PopulateCurveDI(OutDI, TypeStr, Keys, Interp, PopError))
			{
				return MakeErrorJson(PopError);
			}

			if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(OverridePin.GetOwningNode()))
			{
				OwningNode->MarkNodeRequiresSynchronization(TEXT("BlueprintMCP set_module_input curve"), true);
			}
			DynNode->MarkNodeRequiresSynchronization(TEXT("BlueprintMCP set_module_input curve"), true);
		}

		Emitter->MarkPackageDirty();
		RequestEmitterRecompile(Emitter);
		const bool bSaved = SaveGenericPackage(Emitter);

		TSharedRef<FJsonObject> CurveResult = MakeShared<FJsonObject>();
		CurveResult->SetStringField(TEXT("emitter"), Emitter->GetPathName());
		CurveResult->SetStringField(TEXT("moduleName"), ModuleNode->GetFunctionName());
		CurveResult->SetStringField(TEXT("input"), InputName);
		CurveResult->SetStringField(TEXT("type"), InputType.GetName());
		CurveResult->SetStringField(TEXT("valueMode"), TEXT("curve"));
		CurveResult->SetNumberField(TEXT("keyCount"), Keys.Num());
		CurveResult->SetBoolField(TEXT("reusedExistingCurve"), bReused);
		CurveResult->SetBoolField(TEXT("saved"), bSaved);
		return JsonToString(CurveResult);
	}

	// ---- linked-parameter mode -----------------------------------------------
	// CLAUDE-NOTE: valueMode="link" binds the module input to READ FROM another
	// Niagara parameter — typically a System User Parameter (User.*), which is how a
	// scene Blueprint / RenderStream drives the value at runtime (the "controller
	// drives many" pattern). Mirrors UNiagaraStackFunctionInput::SetLinkedParameterValue:
	// graft a ParameterMapGet for the linked var onto the input's override pin, then
	// rebind the module node's input name to the linked script-variable id.
	// SetLinkedParameterValueForFunctionInput checkf-asserts the override pin is
	// unlinked, so switching FROM a curve/dynamic override via MCP isn't supported —
	// we return a clean error rather than tripping the assert. (A prior CONSTANT
	// override only sets the pin DefaultValue, not LinkedTo, so linking over a
	// constant is fine — the link takes precedence.)
	if (ValueMode.Equals(TEXT("link"), ESearchCase::IgnoreCase))
	{
		FString LinkedName;
		if (!Json->TryGetStringField(TEXT("linkedParameter"), LinkedName) || LinkedName.IsEmpty())
		{
			return MakeErrorJson(TEXT("valueMode='link' requires 'linkedParameter' (e.g. 'User.RiseSpeed')"));
		}
		// Auto-prefix bare names to the User namespace, matching add_user_parameter.
		if (!LinkedName.Contains(TEXT(".")))
		{
			LinkedName = FString::Printf(TEXT("User.%s"), *LinkedName);
		}

		const FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*InputName));
		const FNiagaraParameterHandle AliasedHandle =
			FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);

		Emitter->Modify();
		Graph->Modify();

		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

		if (OverridePin.LinkedTo.Num() > 0)
		{
			return MakeErrorJson(TEXT("This input already has a linked/curve override. Clear it in the Niagara editor before binding it to a parameter (switching override types via MCP is not yet supported)."));
		}

		// Exact-typed link: empty KnownParameters means no static/position alternate
		// substitution — the linked parameter is read at the input's own type.
		const FNiagaraVariableBase LinkedParam(InputType, FName(*LinkedName));
		TSet<FNiagaraVariableBase> KnownParameters;
		// CLAUDE-NOTE: SetLinkedParameterValueForFunctionInput does the essential work —
		// it grafts a UNiagaraNodeParameterMapGet reading the linked parameter and wires
		// its output to the input's override pin. That graph topology is what the compiler
		// traces, so the value flows at runtime. The engine's stack code also calls
		// GetScriptVariableIdForLinkedModuleParameter + UNiagaraNodeFunctionCall::UpdateInputNameBinding
		// afterward to sync the module's bound-name bookkeeping, but BOTH of those are
		// non-exported (no NIAGARAEDITOR_API/NIAGARA_API) → LNK2019 from this plugin. They are
		// metadata-sync only (matters for rename propagation / stack UI), not for the binding
		// to evaluate, so we omit them. The link works; it just won't auto-follow a later
		// rename of the User Parameter.
		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(OverridePin, LinkedParam, KnownParameters);

		if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(OverridePin.GetOwningNode()))
		{
			OwningNode->MarkNodeRequiresSynchronization(TEXT("BlueprintMCP set_module_input link"), true);
		}
		ModuleNode->MarkNodeRequiresSynchronization(TEXT("BlueprintMCP set_module_input link"), true);

		Emitter->MarkPackageDirty();
		RequestEmitterRecompile(Emitter);
		const bool bSaved = SaveGenericPackage(Emitter);

		TSharedRef<FJsonObject> LinkResult = MakeShared<FJsonObject>();
		LinkResult->SetStringField(TEXT("emitter"), Emitter->GetPathName());
		LinkResult->SetStringField(TEXT("moduleName"), ModuleNode->GetFunctionName());
		LinkResult->SetStringField(TEXT("input"), InputName);
		LinkResult->SetStringField(TEXT("type"), InputType.GetName());
		LinkResult->SetStringField(TEXT("valueMode"), TEXT("link"));
		LinkResult->SetStringField(TEXT("linkedParameter"), LinkedName);
		LinkResult->SetBoolField(TEXT("saved"), bSaved);
		return JsonToString(LinkResult);
	}

	// Build the value first so we can fail fast on a shape mismatch.
	FNiagaraVariable ValueVar(InputType, NAME_None);
	FString ApplyError;
	if (!ApplyJsonValueToNiagaraVar(ValueVar, Json.ToSharedRef(), ApplyError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to apply value: %s"), *ApplyError));
	}

	// Resolve the aliased "Module.<Input>" handle for this specific module node.
	const FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*InputName));
	const FNiagaraParameterHandle AliasedHandle =
		FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);

	Emitter->Modify();
	Graph->Modify();

	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

	// Mirror UNiagaraStackFunctionInput::SetLocalValue's override-pin branch: serialize the
	// value to the Niagara pin default-value string, then write it onto the override pin.
	FString PinDefaultValue;
	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	if (!NiagaraSchema->TryGetPinDefaultValueFromNiagaraVariable(ValueVar, PinDefaultValue))
	{
		return MakeErrorJson(TEXT("Could not serialize value to a pin default for this type"));
	}

	OverridePin.Modify();
	OverridePin.DefaultValue = PinDefaultValue;
	if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(OverridePin.GetOwningNode()))
	{
		OwningNode->MarkNodeRequiresSynchronization(TEXT("BlueprintMCP set_module_input"), true);
	}

	Emitter->MarkPackageDirty();
	RequestEmitterRecompile(Emitter);
	const bool bSaved = SaveGenericPackage(Emitter);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetStringField(TEXT("moduleName"), ModuleNode->GetFunctionName());
	Result->SetStringField(TEXT("input"), InputName);
	Result->SetStringField(TEXT("type"), InputType.GetName());
	Result->SetStringField(TEXT("pinDefaultValue"), PinDefaultValue);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleSetSystemModuleInput — set a constant on a SYSTEM-stage module
// ============================================================
// CLAUDE-NOTE: System Spawn/Update scripts live on the UNiagaraSystem, not on any emitter,
// so set_module_input (emitter-scoped) can't reach them. This sets a CONSTANT on a system-
// stage module identified by NAME (e.g. "System State") — the fix for System State > Loop
// Duration, which the empty-system factory seeds at 0 (system clock never advances). Both
// system scripts share one source graph; we find the stage's output node, walk its module
// chain, match the function name, then reuse the same override-pin write set_module_input
// uses. Constant only (curve/link not needed for system lifecycle settings).
FString FBlueprintMCPServer::HandleSetSystemModuleInput(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) { return MakeErrorJson(TEXT("Invalid JSON body")); }

	FString SystemNameOrPath, Stage, ModuleName, InputName, TypeStr;
	if (!Json->TryGetStringField(TEXT("system"), SystemNameOrPath) || SystemNameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: system"));
	}
	if (!Json->TryGetStringField(TEXT("stage"), Stage) || Stage.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: stage (SystemSpawn|SystemUpdate)"));
	}
	if (!Json->TryGetStringField(TEXT("module"), ModuleName) || ModuleName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: module (module name, e.g. 'System State')"));
	}
	if (!Json->TryGetStringField(TEXT("input"), InputName) || InputName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: input (e.g. 'Loop Duration')"));
	}
	if (!Json->TryGetStringField(TEXT("type"), TypeStr) || TypeStr.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: type (float|int|bool|vec2|vec3|vec4|color)"));
	}

	ENiagaraScriptUsage Usage;
	if (Stage.Equals(TEXT("SystemSpawn"), ESearchCase::IgnoreCase))       { Usage = ENiagaraScriptUsage::SystemSpawnScript; }
	else if (Stage.Equals(TEXT("SystemUpdate"), ESearchCase::IgnoreCase)) { Usage = ENiagaraScriptUsage::SystemUpdateScript; }
	else { return MakeErrorJson(TEXT("stage must be 'SystemSpawn' or 'SystemUpdate'")); }

	FNiagaraTypeDefinition InputType;
	if (!ResolveNiagaraType(TypeStr, InputType))
	{
		return MakeErrorJson(FString::Printf(TEXT("Unsupported type '%s'"), *TypeStr));
	}

	UNiagaraSystem* System = FindNiagaraSystemByNameOrPath(SystemNameOrPath);
	if (!System)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraSystem '%s' not found"), *SystemNameOrPath));
	}

	// Both system scripts share one source graph; the spawn script's source is fine for either stage.
	UNiagaraScript* SysScript = System->GetSystemSpawnScript();
	UNiagaraScriptSource* Source = SysScript ? Cast<UNiagaraScriptSource>(SysScript->GetLatestSource()) : nullptr;
	UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;
	if (!Graph)
	{
		return MakeErrorJson(TEXT("Could not resolve the system script graph"));
	}

	UNiagaraNodeOutput* OutputNode = FindStageOutputNode(Graph, Usage);
	if (!OutputNode)
	{
		return MakeErrorJson(FString::Printf(TEXT("No %s output node in the system graph"), *Stage));
	}

	TArray<UNiagaraNodeFunctionCall*> Modules;
	GetOrderedStageModules(OutputNode, Modules);

	UNiagaraNodeFunctionCall* ModuleNode = nullptr;
	TArray<FString> Available;
	for (UNiagaraNodeFunctionCall* M : Modules)
	{
		if (!M) { continue; }
		const FString Fn = M->GetFunctionName();
		Available.Add(Fn);
		// Match exact or relaxed (ignore spaces/case): "System State" vs "SystemState".
		const FString FnCompact = Fn.Replace(TEXT(" "), TEXT(""));
		const FString WantCompact = ModuleName.Replace(TEXT(" "), TEXT(""));
		if (Fn.Equals(ModuleName, ESearchCase::IgnoreCase) || FnCompact.Equals(WantCompact, ESearchCase::IgnoreCase))
		{
			ModuleNode = M;
			break;
		}
	}
	if (!ModuleNode)
	{
		return MakeErrorJson(FString::Printf(TEXT("No module '%s' on system %s. Available: %s"),
			*ModuleName, *Stage, *FString::Join(Available, TEXT(", "))));
	}

	// Validate the input exists on this module and the type matches (snap to exact spelling/case).
	{
		TArray<FNiagaraVariable> ModuleInputs;
		FCompileConstantResolver Resolver;
		FNiagaraStackGraphUtilities::GetStackFunctionInputs(
			*ModuleNode, ModuleInputs, Resolver,
			FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly,
			/*bIgnoreDisabled*/ false);

		const FNiagaraVariable* MatchedInput = nullptr;
		for (const FNiagaraVariable& Var : ModuleInputs)
		{
			FString BareName = Var.GetName().ToString();
			if (BareName.StartsWith(TEXT("Module."))) { BareName = BareName.RightChop(7); }
			if (BareName.Equals(InputName, ESearchCase::IgnoreCase))
			{
				MatchedInput = &Var;
				InputName = BareName;
				break;
			}
		}
		if (!MatchedInput)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Input '%s' not found on module '%s'."), *InputName, *ModuleNode->GetFunctionName()));
		}
		if (MatchedInput->GetType() != InputType)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Input '%s' on '%s' is type '%s', not '%s'."),
				*InputName, *ModuleNode->GetFunctionName(), *MatchedInput->GetType().GetName(), *InputType.GetName()));
		}
	}

	FNiagaraVariable ValueVar(InputType, NAME_None);
	FString ApplyError;
	if (!ApplyJsonValueToNiagaraVar(ValueVar, Json.ToSharedRef(), ApplyError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to apply value: %s"), *ApplyError));
	}

	const FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*InputName));
	const FNiagaraParameterHandle AliasedHandle =
		FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);

	System->Modify();
	Graph->Modify();

	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

	FString PinDefaultValue;
	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	if (!NiagaraSchema->TryGetPinDefaultValueFromNiagaraVariable(ValueVar, PinDefaultValue))
	{
		return MakeErrorJson(TEXT("Could not serialize value to a pin default for this type"));
	}

	OverridePin.Modify();
	OverridePin.DefaultValue = PinDefaultValue;
	if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(OverridePin.GetOwningNode()))
	{
		OwningNode->MarkNodeRequiresSynchronization(TEXT("BlueprintMCP set_system_module_input"), true);
	}

	System->MarkPackageDirty();
	System->RequestCompile(false);
	const bool bSaved = SaveGenericPackage(System);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system"), System->GetPathName());
	Result->SetStringField(TEXT("stage"), Stage);
	Result->SetStringField(TEXT("module"), ModuleNode->GetFunctionName());
	Result->SetStringField(TEXT("input"), InputName);
	Result->SetStringField(TEXT("type"), InputType.GetName());
	Result->SetStringField(TEXT("pinDefaultValue"), PinDefaultValue);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleListModuleLibrary — list module scripts valid for a stage
// ============================================================

FString FBlueprintMCPServer::HandleListModuleLibrary(const TMap<FString, FString>& Params)
{
	// Optional stage filter: only modules whose usage bitmask supports it.
	bool bHasStageFilter = false;
	ENiagaraScriptUsage StageUsage = ENiagaraScriptUsage::ParticleSpawnScript;
	if (const FString* StageVal = Params.Find(TEXT("stage")))
	{
		if (!StageVal->IsEmpty())
		{
			if (!StageToScriptUsage(*StageVal, StageUsage))
			{
				return MakeErrorJson(FString::Printf(TEXT("Unknown stage '%s'"), **StageVal));
			}
			bHasStageFilter = true;
		}
	}

	FString PathFilter;
	if (const FString* PathVal = Params.Find(TEXT("path"))) { PathFilter = *PathVal; }

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = ARM.Get();

	TArray<FAssetData> ScriptAssets;
	Registry.GetAssetsByClass(UNiagaraScript::StaticClass()->GetClassPathName(), ScriptAssets, false);

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FAssetData& AssetData : ScriptAssets)
	{
		const FString PathName = AssetData.GetSoftObjectPath().ToString();
		if (!PathFilter.IsEmpty() && !PathName.StartsWith(PathFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		UNiagaraScript* Script = Cast<UNiagaraScript>(AssetData.GetAsset());
		if (!Script || !Script->IsEquivalentUsage(ENiagaraScriptUsage::Module))
		{
			continue;
		}

		const FVersionedNiagaraScriptData* ScriptData = Script->GetLatestScriptData();
		if (!ScriptData) { continue; }

		if (bHasStageFilter &&
			!UNiagaraScript::IsSupportedUsageContextForBitmask(ScriptData->ModuleUsageBitmask, StageUsage))
		{
			continue;
		}

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		Item->SetStringField(TEXT("path"), PathName);
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), Items.Num());
	Result->SetArrayField(TEXT("modules"), Items);
	if (bHasStageFilter) { Result->SetStringField(TEXT("stageFilter"), *Params.Find(TEXT("stage"))); }
	return JsonToString(Result);
}

// ============================================================
// HandleRemoveNiagaraRenderer — remove a renderer by index
// ============================================================

FString FBlueprintMCPServer::HandleRemoveNiagaraRenderer(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}
	int32 Index = 0;
	if (!Json->TryGetNumberField(TEXT("index"), Index))
	{
		return MakeErrorJson(TEXT("Missing required field: index (renderer index from get_niagara_emitter_summary)"));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	if (!Data)
	{
		return MakeErrorJson(TEXT("Emitter has no latest version data"));
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = Data->GetRenderers();
	if (!Renderers.IsValidIndex(Index))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Renderer index %d out of range (emitter has %d renderer(s))"), Index, Renderers.Num()));
	}

	UNiagaraRendererProperties* Renderer = Renderers[Index];
	const FString RemovedClass = Renderer ? Renderer->GetClass()->GetName() : TEXT("<null>");

	Emitter->Modify();
	Emitter->RemoveRenderer(Renderer, Emitter->GetExposedVersion().VersionGuid);
	Emitter->MarkPackageDirty();
	RequestEmitterRecompile(Emitter);
	const bool bSaved = SaveGenericPackage(Emitter);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetStringField(TEXT("removedClass"), RemovedClass);
	Result->SetNumberField(TEXT("rendererCount"), Data->GetRenderers().Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleRemoveUserParameter — remove a User Parameter from a system
// ============================================================

FString FBlueprintMCPServer::HandleRemoveUserParameter(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SystemNameOrPath, ParamName;
	if (!Json->TryGetStringField(TEXT("system"), SystemNameOrPath) || SystemNameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: system"));
	}
	if (!Json->TryGetStringField(TEXT("name"), ParamName) || ParamName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: name"));
	}

	UNiagaraSystem* System = FindNiagaraSystemByNameOrPath(SystemNameOrPath);
	if (!System)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraSystem '%s' not found"), *SystemNameOrPath));
	}

	const FString FullName = ParamName.StartsWith(TEXT("User.")) ? ParamName : (TEXT("User.") + ParamName);

	FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
	TArray<FNiagaraVariable> Existing;
	Store.GetParameters(Existing);

	FNiagaraVariable Target;
	for (const FNiagaraVariable& Var : Existing)
	{
		if (Var.GetName().ToString().Equals(FullName, ESearchCase::IgnoreCase))
		{
			Target = Var;
			break;
		}
	}
	if (!Target.IsValid())
	{
		return MakeErrorJson(FString::Printf(TEXT("User parameter '%s' not found"), *FullName));
	}

	System->Modify();
	const bool bRemoved = Store.RemoveParameter(Target);
	System->MarkPackageDirty();
	const bool bSaved = SaveGenericPackage(System);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system"), System->GetPathName());
	Result->SetStringField(TEXT("name"), FullName);
	Result->SetBoolField(TEXT("removed"), bRemoved);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleRemoveEmitterFromSystem — detach an emitter handle from a system
// ============================================================

FString FBlueprintMCPServer::HandleRemoveEmitterFromSystem(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SystemNameOrPath, HandleName;
	if (!Json->TryGetStringField(TEXT("system"), SystemNameOrPath) || SystemNameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: system"));
	}
	if (!Json->TryGetStringField(TEXT("handleName"), HandleName) || HandleName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: handleName (the emitter handle name shown by get_niagara_system_summary)"));
	}

	UNiagaraSystem* System = FindNiagaraSystemByNameOrPath(SystemNameOrPath);
	if (!System)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraSystem '%s' not found"), *SystemNameOrPath));
	}

	// CLAUDE-NOTE: copy the handle out before removing — RemoveEmitterHandle mutates the
	// array, so we can't hold a reference into GetEmitterHandles() across the call.
	bool bFound = false;
	FNiagaraEmitterHandle ToRemove;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString().Equals(HandleName, ESearchCase::IgnoreCase))
		{
			ToRemove = Handle;
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		return MakeErrorJson(FString::Printf(TEXT("No emitter handle named '%s' in system"), *HandleName));
	}

	System->Modify();
	System->RemoveEmitterHandle(ToRemove);
	System->RequestCompile(false);
	System->MarkPackageDirty();
	const bool bSaved = SaveGenericPackage(System);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system"), System->GetPathName());
	Result->SetStringField(TEXT("removedHandle"), HandleName);
	Result->SetNumberField(TEXT("emitterCount"), System->GetEmitterHandles().Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

// ============================================================
// HandleListEmitterModules — list stack modules per stage on an emitter
// ============================================================

FString FBlueprintMCPServer::HandleListEmitterModules(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}

	// Optional stage filter; if absent, report all four stages.
	FString StageFilter;
	bool bHasStageFilter = Json->TryGetStringField(TEXT("stage"), StageFilter) && !StageFilter.IsEmpty();
	ENiagaraScriptUsage FilterUsage = ENiagaraScriptUsage::ParticleSpawnScript;
	if (bHasStageFilter && !StageToScriptUsage(StageFilter, FilterUsage))
	{
		return MakeErrorJson(FString::Printf(TEXT("Unknown stage '%s'"), *StageFilter));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	UNiagaraGraph* Graph = GetEmitterGraph(Data);
	if (!Graph)
	{
		return MakeErrorJson(TEXT("Could not resolve emitter node graph"));
	}

	// CLAUDE-NOTE: stage label <-> usage pairs in stack order. The module GUIDs returned
	// here are what set_module_input / remove-module style tools target on EXISTING modules
	// (you no longer need to have just added the module to know its GUID).
	struct FStageDef { const TCHAR* Label; ENiagaraScriptUsage Usage; };
	static const FStageDef Stages[] = {
		{ TEXT("EmitterSpawn"),   ENiagaraScriptUsage::EmitterSpawnScript },
		{ TEXT("EmitterUpdate"),  ENiagaraScriptUsage::EmitterUpdateScript },
		{ TEXT("ParticleSpawn"),  ENiagaraScriptUsage::ParticleSpawnScript },
		{ TEXT("ParticleUpdate"), ENiagaraScriptUsage::ParticleUpdateScript },
	};

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FStageDef& StageDef : Stages)
	{
		if (bHasStageFilter && !UNiagaraScript::IsEquivalentUsage(StageDef.Usage, FilterUsage))
		{
			continue;
		}

		UNiagaraNodeOutput* OutputNode = FindStageOutputNode(Graph, StageDef.Usage);
		if (!OutputNode) { continue; }

		TArray<UNiagaraNodeFunctionCall*> Modules;
		GetOrderedStageModules(OutputNode, Modules);

		int32 OrderIndex = 0;
		for (UNiagaraNodeFunctionCall* Module : Modules)
		{
			if (!Module) { continue; }
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("stage"), StageDef.Label);
			Item->SetNumberField(TEXT("order"), OrderIndex++);
			Item->SetStringField(TEXT("name"), Module->GetFunctionName());
			Item->SetStringField(TEXT("nodeGuid"), Module->NodeGuid.ToString());
			if (Module->FunctionScript)
			{
				Item->SetStringField(TEXT("scriptPath"), Module->FunctionScript->GetPathName());
			}
			Item->SetBoolField(TEXT("enabled"), Module->IsNodeEnabled());
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetNumberField(TEXT("count"), Items.Num());
	Result->SetArrayField(TEXT("modules"), Items);
	if (bHasStageFilter) { Result->SetStringField(TEXT("stageFilter"), StageFilter); }
	return JsonToString(Result);
}

// ============================================================
// HandleListModuleInputs — list a module's input names + types
// ============================================================

FString FBlueprintMCPServer::HandleListModuleInputs(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath, ModuleGuidStr;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}
	if (!Json->TryGetStringField(TEXT("moduleNodeGuid"), ModuleGuidStr) || ModuleGuidStr.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: moduleNodeGuid (from list_emitter_modules)"));
	}

	FGuid ModuleGuid;
	if (!FGuid::Parse(ModuleGuidStr, ModuleGuid))
	{
		return MakeErrorJson(TEXT("moduleNodeGuid is not a valid GUID"));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	UNiagaraGraph* Graph = GetEmitterGraph(Data);
	if (!Graph)
	{
		return MakeErrorJson(TEXT("Could not resolve emitter node graph"));
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNodeByGuid(Graph, ModuleGuid);
	if (!ModuleNode)
	{
		return MakeErrorJson(FString::Printf(TEXT("No module node with GUID '%s' in this emitter"), *ModuleGuidStr));
	}

	// CLAUDE-NOTE: a default-constructed FCompileConstantResolver is what the engine's own
	// input binder uses outside a full compile context — sufficient to enumerate a module's
	// top-level inputs. ModuleInputsOnly excludes nested dynamic-input sub-function inputs.
	TArray<FNiagaraVariable> InputVars;
	FCompileConstantResolver Resolver;
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*ModuleNode, InputVars, Resolver,
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly,
		/*bIgnoreDisabled*/ false);

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FNiagaraVariable& Var : InputVars)
	{
		const FString FullName = Var.GetName().ToString();
		// set_module_input expects the bare input name (it re-aliases to Module.<name>).
		FString InputName = FullName;
		if (InputName.StartsWith(TEXT("Module.")))
		{
			InputName = InputName.RightChop(7);
		}
		const FString TypeStr = NiagaraTypeToInputTypeString(Var.GetType());

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("input"), InputName);
		Item->SetStringField(TEXT("fullName"), FullName);
		Item->SetStringField(TEXT("niagaraType"), Var.GetType().GetName());
		Item->SetStringField(TEXT("type"), TypeStr);                 // "" if set_module_input can't write it
		Item->SetBoolField(TEXT("settable"), !TypeStr.IsEmpty());
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetStringField(TEXT("moduleName"), ModuleNode->GetFunctionName());
	Result->SetStringField(TEXT("moduleNodeGuid"), ModuleGuidStr);
	Result->SetNumberField(TEXT("count"), Items.Num());
	Result->SetArrayField(TEXT("inputs"), Items);
	return JsonToString(Result);
}

// ============================================================
// HandleSetRendererProperty — set any property on a renderer by index
// ============================================================

FString FBlueprintMCPServer::HandleSetRendererProperty(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString NameOrPath, PropertyName, PropertyValue;
	if (!Json->TryGetStringField(TEXT("emitter"), NameOrPath) || NameOrPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: emitter"));
	}
	int32 Index = 0;
	if (!Json->TryGetNumberField(TEXT("index"), Index))
	{
		return MakeErrorJson(TEXT("Missing required field: index (renderer index from get_niagara_emitter_summary)"));
	}
	if (!Json->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: property (e.g. 'Material')"));
	}
	if (!Json->TryGetStringField(TEXT("value"), PropertyValue))
	{
		return MakeErrorJson(TEXT("Missing required field: value (UE import-text, e.g. an asset path '/Game/.../M_Glow.M_Glow')"));
	}

	UNiagaraEmitter* Emitter = FindNiagaraEmitterByNameOrPath(NameOrPath);
	if (!Emitter)
	{
		return MakeErrorJson(FString::Printf(TEXT("NiagaraEmitter '%s' not found"), *NameOrPath));
	}

	FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
	if (!Data)
	{
		return MakeErrorJson(TEXT("Emitter has no latest version data"));
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = Data->GetRenderers();
	if (!Renderers.IsValidIndex(Index))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Renderer index %d out of range (emitter has %d renderer(s))"), Index, Renderers.Num()));
	}

	UNiagaraRendererProperties* Renderer = Renderers[Index];
	if (!Renderer)
	{
		return MakeErrorJson(FString::Printf(TEXT("Renderer at index %d is null"), Index));
	}

	// CLAUDE-NOTE: generic reflection set, mirroring HandleSetActorProperty. The renderer is
	// a UObject so FindFProperty + ImportText_Direct work; e.g. set "Material" on a sprite/
	// ribbon renderer to an asset path. PostEditChangeProperty + RequestEmitterRecompile so
	// the change takes and any referencing system rebuilds.
	FProperty* Prop = FindFProperty<FProperty>(Renderer->GetClass(), *PropertyName);
	if (!Prop)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Property '%s' not found on renderer class '%s'."),
			*PropertyName, *Renderer->GetClass()->GetName()));
	}

	Renderer->Modify();
	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Renderer);
	const TCHAR* ImportResult = Prop->ImportText_Direct(*PropertyValue, PropAddr, Renderer, 0);
	if (!ImportResult)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Failed to set '%s' to '%s' — value incompatible with type '%s'"),
			*PropertyName, *PropertyValue, *Prop->GetCPPType()));
	}

	FPropertyChangedEvent ChangedEvent(Prop);
	Renderer->PostEditChangeProperty(ChangedEvent);
	Emitter->MarkPackageDirty();
	RequestEmitterRecompile(Emitter);
	const bool bSaved = SaveGenericPackage(Emitter);

	FString ExportedValue;
	Prop->ExportTextItem_Direct(ExportedValue, PropAddr, nullptr, Renderer, PPF_None);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("emitter"), Emitter->GetPathName());
	Result->SetNumberField(TEXT("index"), Index);
	Result->SetStringField(TEXT("rendererClass"), Renderer->GetClass()->GetName());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetStringField(TEXT("value"), ExportedValue);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}
