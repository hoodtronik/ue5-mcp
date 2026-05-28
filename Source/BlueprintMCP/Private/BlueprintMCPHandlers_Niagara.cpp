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
#include "NiagaraRendererProperties.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitterFactoryNew.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
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
