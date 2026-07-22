#include "BlueprintMCPServer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================
// HandleBuildGraph — create many nodes, wire them, and set pin
// defaults in a single request, with ONE package save at the end.
//
// CLAUDE-NOTE: this exists because per-node authoring is dominated by
// SaveBlueprintPackage (compile + .uasset write) running once per add_node call.
// A 10-node graph via the single-node tools costs ~10 saves + ~25 HTTP round
// trips; here it costs 1 save and 1 round trip.
//
// Node creation intentionally delegates to HandleAddNode via an in-process JSON
// body rather than duplicating its ~500-line node-type dispatch. The JSON
// round-trip is microseconds against a package save, and it guarantees
// build_graph supports exactly the node types add_node does, forever, with no
// drift. The `deferSave` flag on that call suppresses the per-node save; this
// function then owns saving exactly once.
// ============================================================

namespace
{
	/** A node created (or resolved) during this build, addressable by its caller-supplied ref. */
	struct FBuiltNode
	{
		FString Ref;
		FString NodeId;
	};

	/**
	 * Split a "RefOrGuid.PinName" endpoint. Pin names themselves never contain '.', but object
	 * paths used as refs might, so split on the LAST dot.
	 */
	bool SplitEndpoint(const FString& Endpoint, FString& OutRef, FString& OutPin)
	{
		int32 DotIndex = INDEX_NONE;
		if (!Endpoint.FindLastChar(TEXT('.'), DotIndex) || DotIndex <= 0 || DotIndex >= Endpoint.Len() - 1)
		{
			return false;
		}
		OutRef = Endpoint.Left(DotIndex);
		OutPin = Endpoint.Mid(DotIndex + 1);
		return true;
	}

	/**
	 * Resolve a pin on a node, supporting the aliases an agent is likely to reach for instead of
	 * the engine's real pin names.
	 *
	 * CLAUDE-NOTE: aliases resolve POSITIONALLY (first exec in / first exec out / first data out)
	 * because real pin names vary per node class — a CallFunction's data output is "ReturnValue",
	 * a VariableGet's is the variable's own name, and a pure function has no exec pins at all.
	 * Without aliases an agent has to round-trip get_pin_info before it can wire anything, which
	 * defeats the point of batching. Exact matches are tried first so aliases never shadow a real pin.
	 */
	UEdGraphPin* ResolvePin(UEdGraphNode* Node, const FString& PinName)
	{
		if (!Node)
		{
			return nullptr;
		}

		// Exact match always wins, so a real pin named e.g. "Value" is never shadowed by an alias.
		if (UEdGraphPin* Exact = Node->FindPin(FName(*PinName)))
		{
			return Exact;
		}

		const FString Lower = PinName.ToLower();

		auto FirstPin = [Node](EEdGraphPinDirection Dir, bool bExec) -> UEdGraphPin*
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->bHidden || Pin->Direction != Dir)
				{
					continue;
				}
				const bool bIsExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
				if (bIsExec == bExec)
				{
					return Pin;
				}
			}
			return nullptr;
		};

		if (Lower == TEXT("execute") || Lower == TEXT("exec") || Lower == TEXT("in"))
		{
			return FirstPin(EGPD_Input, /*bExec=*/true);
		}
		if (Lower == TEXT("then") || Lower == TEXT("output") || Lower == TEXT("out"))
		{
			return FirstPin(EGPD_Output, /*bExec=*/true);
		}
		if (Lower == TEXT("value") || Lower == TEXT("result"))
		{
			return FirstPin(EGPD_Output, /*bExec=*/false);
		}
		// Branch convenience: True/False map to the schema's real pin names.
		if (Lower == TEXT("true"))
		{
			return Node->FindPin(UEdGraphSchema_K2::PN_Then);
		}
		if (Lower == TEXT("false"))
		{
			return Node->FindPin(UEdGraphSchema_K2::PN_Else);
		}

		// Case-insensitive fallback before giving up.
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName.ToString().ToLower() == Lower)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** Human-readable pin list, so a failed wire tells the agent what it could have used. */
	FString DescribeAvailablePins(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return FString();
		}
		TArray<FString> In, Out;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden)
			{
				continue;
			}
			(Pin->Direction == EGPD_Input ? In : Out).Add(Pin->PinName.ToString());
		}
		return FString::Printf(TEXT("inputs: [%s]; outputs: [%s]"),
			*FString::Join(In, TEXT(", ")), *FString::Join(Out, TEXT(", ")));
	}
}

FString FBlueprintMCPServer::HandleBuildGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	const FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	const FString GraphName = Json->GetStringField(TEXT("graph"));
	if (BlueprintName.IsEmpty() || GraphName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required fields: blueprint, graph"));
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* PinDefaultsArray = nullptr;
	Json->TryGetArrayField(TEXT("nodes"), NodesArray);
	Json->TryGetArrayField(TEXT("connections"), ConnectionsArray);
	Json->TryGetArrayField(TEXT("pinDefaults"), PinDefaultsArray);

	if ((!NodesArray || NodesArray->Num() == 0) &&
		(!ConnectionsArray || ConnectionsArray->Num() == 0) &&
		(!PinDefaultsArray || PinDefaultsArray->Num() == 0))
	{
		return MakeErrorJson(TEXT("Nothing to do: supply at least one of nodes, connections, pinDefaults"));
	}

	// Note: there is deliberately no `compile` flag — SaveBlueprintPackage always compiles, so
	// the batch is always left in a compiled state. See the save block below.
	const bool bDryRun = Json->HasField(TEXT("dryRun")) && Json->GetBoolField(TEXT("dryRun"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP)
	{
		return MakeErrorJson(LoadError);
	}

	const FString DecodedGraphName = UrlDecode(GraphName);
	UEdGraph* TargetGraph = nullptr;
	{
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName().Equals(DecodedGraphName, ESearchCase::IgnoreCase))
			{
				TargetGraph = Graph;
				break;
			}
		}
	}
	if (!TargetGraph)
	{
		return MakeErrorJson(FString::Printf(TEXT("Graph '%s' not found in blueprint '%s'"),
			*DecodedGraphName, *BlueprintName));
	}

	// ------------------------------------------------------------------
	// Dry run: validate structure without mutating anything.
	// ------------------------------------------------------------------
	if (bDryRun)
	{
		TSet<FString> KnownRefs;
		TArray<FString> Problems;

		if (NodesArray)
		{
			for (int32 i = 0; i < NodesArray->Num(); ++i)
			{
				TSharedPtr<FJsonObject> Spec = (*NodesArray)[i]->AsObject();
				if (!Spec.IsValid())
				{
					Problems.Add(FString::Printf(TEXT("nodes[%d]: not an object"), i));
					continue;
				}
				const FString Ref = Spec->GetStringField(TEXT("ref"));
				const FString NodeType = Spec->GetStringField(TEXT("nodeType"));
				if (Ref.IsEmpty())
				{
					Problems.Add(FString::Printf(TEXT("nodes[%d]: missing 'ref'"), i));
				}
				else if (KnownRefs.Contains(Ref))
				{
					Problems.Add(FString::Printf(TEXT("nodes[%d]: duplicate ref '%s'"), i, *Ref));
				}
				else
				{
					KnownRefs.Add(Ref);
				}
				if (NodeType.IsEmpty())
				{
					Problems.Add(FString::Printf(TEXT("nodes[%d]: missing 'nodeType'"), i));
				}
			}
		}

		if (ConnectionsArray)
		{
			for (int32 i = 0; i < ConnectionsArray->Num(); ++i)
			{
				TSharedPtr<FJsonObject> Conn = (*ConnectionsArray)[i]->AsObject();
				if (!Conn.IsValid())
				{
					Problems.Add(FString::Printf(TEXT("connections[%d]: not an object"), i));
					continue;
				}
				const FString From = Conn->GetStringField(TEXT("from"));
				const FString To = Conn->GetStringField(TEXT("to"));
				FString R, P;
				// Refs that aren't in this batch are assumed to be existing node GUIDs; that can
				// only be checked against the live graph at apply time, so it isn't a dry-run error.
				if (From.IsEmpty() || !SplitEndpoint(From, R, P))
				{
					Problems.Add(FString::Printf(TEXT("connections[%d]: 'from' must be \"Ref.PinName\""), i));
				}
				if (To.IsEmpty() || !SplitEndpoint(To, R, P))
				{
					Problems.Add(FString::Printf(TEXT("connections[%d]: 'to' must be \"Ref.PinName\""), i));
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), Problems.Num() == 0);
		Result->SetBoolField(TEXT("dryRun"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("graph"), DecodedGraphName);
		Result->SetNumberField(TEXT("nodeCount"), NodesArray ? NodesArray->Num() : 0);
		Result->SetNumberField(TEXT("connectionCount"), ConnectionsArray ? ConnectionsArray->Num() : 0);
		Result->SetNumberField(TEXT("pinDefaultCount"), PinDefaultsArray ? PinDefaultsArray->Num() : 0);
		TArray<TSharedPtr<FJsonValue>> ProblemVals;
		for (const FString& P : Problems)
		{
			ProblemVals.Add(MakeShared<FJsonValueString>(P));
		}
		Result->SetArrayField(TEXT("problems"), ProblemVals);
		return JsonToString(Result);
	}

	// ------------------------------------------------------------------
	// Phase 1 — create nodes.
	// ------------------------------------------------------------------
	TMap<FString, FString> RefToNodeId;
	TArray<TSharedPtr<FJsonValue>> NodeResults;
	int32 NodesCreated = 0, NodesFailed = 0;

	if (NodesArray)
	{
		int32 AutoX = 0;
		for (int32 i = 0; i < NodesArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Spec = (*NodesArray)[i]->AsObject();
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);

			if (!Spec.IsValid())
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), TEXT("Not an object"));
				NodeResults.Add(MakeShared<FJsonValueObject>(Entry));
				NodesFailed++;
				continue;
			}

			const FString Ref = Spec->GetStringField(TEXT("ref"));
			Entry->SetStringField(TEXT("ref"), Ref);
			Entry->SetStringField(TEXT("nodeType"), Spec->GetStringField(TEXT("nodeType")));

			if (Ref.IsEmpty())
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), TEXT("Missing 'ref'"));
				NodeResults.Add(MakeShared<FJsonValueObject>(Entry));
				NodesFailed++;
				continue;
			}
			if (RefToNodeId.Contains(Ref))
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Duplicate ref '%s'"), *Ref));
				NodeResults.Add(MakeShared<FJsonValueObject>(Entry));
				NodesFailed++;
				continue;
			}

			// Build the add_node body: copy the caller's spec through verbatim so every
			// node-type-specific field add_node understands keeps working, then override
			// the fields we own.
			TSharedRef<FJsonObject> AddBody = MakeShared<FJsonObject>();
			for (const auto& Pair : Spec->Values)
			{
				AddBody->SetField(Pair.Key, Pair.Value);
			}
			AddBody->SetStringField(TEXT("blueprint"), BlueprintName);
			AddBody->SetStringField(TEXT("graph"), DecodedGraphName);
			AddBody->SetBoolField(TEXT("deferSave"), true);

			// Give unpositioned nodes a readable left-to-right default instead of stacking
			// them all at the origin.
			if (!Spec->HasField(TEXT("posX")) && !Spec->HasField(TEXT("posY")))
			{
				AddBody->SetNumberField(TEXT("posX"), AutoX);
				AddBody->SetNumberField(TEXT("posY"), 0);
				AutoX += 320;
			}

			const FString AddResponse = HandleAddNode(JsonToString(AddBody));
			TSharedPtr<FJsonObject> AddJson = ParseBodyJson(AddResponse);

			FString NewNodeId;
			bool bOk = false;
			if (AddJson.IsValid())
			{
				AddJson->TryGetStringField(TEXT("nodeId"), NewNodeId);
				bOk = AddJson->HasField(TEXT("success")) && AddJson->GetBoolField(TEXT("success")) && !NewNodeId.IsEmpty();
			}

			if (bOk)
			{
				RefToNodeId.Add(Ref, NewNodeId);
				Entry->SetBoolField(TEXT("success"), true);
				Entry->SetStringField(TEXT("nodeId"), NewNodeId);
				NodesCreated++;
			}
			else
			{
				FString Err = TEXT("add_node failed");
				if (AddJson.IsValid())
				{
					AddJson->TryGetStringField(TEXT("error"), Err);
				}
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), Err);
				NodesFailed++;
			}
			NodeResults.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	// Resolve an endpoint ref to a live node: either a ref created above, or a GUID of a node
	// already in the graph (so a batch can wire new nodes onto existing ones).
	auto ResolveNode = [&](const FString& RefOrGuid, FString& OutError) -> UEdGraphNode*
	{
		FString NodeId = RefOrGuid;
		if (const FString* Mapped = RefToNodeId.Find(RefOrGuid))
		{
			NodeId = *Mapped;
		}
		UEdGraphNode* Node = FindNodeByGuid(BP, NodeId);
		if (!Node)
		{
			OutError = FString::Printf(
				TEXT("'%s' is neither a ref created in this call nor an existing node GUID"), *RefOrGuid);
		}
		return Node;
	};

	// ------------------------------------------------------------------
	// Phase 2 — connections.
	// ------------------------------------------------------------------
	TArray<TSharedPtr<FJsonValue>> ConnectionResults;
	int32 ConnectionsMade = 0, ConnectionsFailed = 0;

	if (ConnectionsArray)
	{
		const UEdGraphSchema* Schema = TargetGraph->GetSchema();
		for (int32 i = 0; i < ConnectionsArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Conn = (*ConnectionsArray)[i]->AsObject();
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);

			auto Fail = [&](const FString& Msg)
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), Msg);
				ConnectionResults.Add(MakeShared<FJsonValueObject>(Entry));
				ConnectionsFailed++;
			};

			if (!Conn.IsValid())
			{
				Fail(TEXT("Not an object"));
				continue;
			}

			const FString From = Conn->GetStringField(TEXT("from"));
			const FString To = Conn->GetStringField(TEXT("to"));
			Entry->SetStringField(TEXT("from"), From);
			Entry->SetStringField(TEXT("to"), To);

			FString FromRef, FromPinName, ToRef, ToPinName;
			if (!SplitEndpoint(From, FromRef, FromPinName))
			{
				Fail(TEXT("'from' must be \"RefOrNodeId.PinName\""));
				continue;
			}
			if (!SplitEndpoint(To, ToRef, ToPinName))
			{
				Fail(TEXT("'to' must be \"RefOrNodeId.PinName\""));
				continue;
			}

			FString ResolveError;
			UEdGraphNode* FromNode = ResolveNode(FromRef, ResolveError);
			if (!FromNode)
			{
				Fail(ResolveError);
				continue;
			}
			UEdGraphNode* ToNode = ResolveNode(ToRef, ResolveError);
			if (!ToNode)
			{
				Fail(ResolveError);
				continue;
			}

			UEdGraphPin* FromPin = ResolvePin(FromNode, FromPinName);
			if (!FromPin)
			{
				Fail(FString::Printf(TEXT("Pin '%s' not found on '%s' (%s)"),
					*FromPinName, *FromRef, *DescribeAvailablePins(FromNode)));
				continue;
			}
			UEdGraphPin* ToPin = ResolvePin(ToNode, ToPinName);
			if (!ToPin)
			{
				Fail(FString::Printf(TEXT("Pin '%s' not found on '%s' (%s)"),
					*ToPinName, *ToRef, *DescribeAvailablePins(ToNode)));
				continue;
			}

			if (!Schema)
			{
				Fail(TEXT("Graph has no schema"));
				continue;
			}

			// Let the schema judge compatibility so we surface ITS reason rather than a
			// generic failure — the message is usually directly actionable.
			const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
			if (Response.Response == CONNECT_RESPONSE_DISALLOW)
			{
				Fail(FString::Printf(TEXT("Incompatible pins: %s"), *Response.Message.ToString()));
				continue;
			}

			if (!Schema->TryCreateConnection(FromPin, ToPin))
			{
				Fail(TEXT("TryCreateConnection failed"));
				continue;
			}

			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("fromPin"), FromPin->PinName.ToString());
			Entry->SetStringField(TEXT("toPin"), ToPin->PinName.ToString());
			ConnectionResults.Add(MakeShared<FJsonValueObject>(Entry));
			ConnectionsMade++;
		}
	}

	// ------------------------------------------------------------------
	// Phase 3 — pin defaults.
	// ------------------------------------------------------------------
	TArray<TSharedPtr<FJsonValue>> PinDefaultResults;
	int32 DefaultsSet = 0, DefaultsFailed = 0;

	if (PinDefaultsArray)
	{
		for (int32 i = 0; i < PinDefaultsArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Def = (*PinDefaultsArray)[i]->AsObject();
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);

			auto Fail = [&](const FString& Msg)
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), Msg);
				PinDefaultResults.Add(MakeShared<FJsonValueObject>(Entry));
				DefaultsFailed++;
			};

			if (!Def.IsValid())
			{
				Fail(TEXT("Not an object"));
				continue;
			}

			const FString NodeRef = Def->GetStringField(TEXT("nodeRef"));
			const FString PinName = Def->GetStringField(TEXT("pinName"));
			const FString Value = Def->GetStringField(TEXT("value"));
			Entry->SetStringField(TEXT("nodeRef"), NodeRef);
			Entry->SetStringField(TEXT("pinName"), PinName);

			if (NodeRef.IsEmpty() || PinName.IsEmpty())
			{
				Fail(TEXT("Missing required fields: nodeRef, pinName"));
				continue;
			}

			FString ResolveError;
			UEdGraphNode* Node = ResolveNode(NodeRef, ResolveError);
			if (!Node)
			{
				Fail(ResolveError);
				continue;
			}

			UEdGraphPin* Pin = ResolvePin(Node, PinName);
			if (!Pin)
			{
				Fail(FString::Printf(TEXT("Pin '%s' not found on '%s' (%s)"),
					*PinName, *NodeRef, *DescribeAvailablePins(Node)));
				continue;
			}
			if (Pin->Direction != EGPD_Input)
			{
				Fail(FString::Printf(TEXT("Pin '%s' is an output pin"), *PinName));
				continue;
			}

			const FString OldValue = Pin->DefaultValue;
			if (const UEdGraphSchema* Schema = Node->GetGraph() ? Node->GetGraph()->GetSchema() : nullptr)
			{
				Schema->TrySetDefaultValue(*Pin, Value);
			}
			else
			{
				Pin->DefaultValue = Value;
			}

			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetStringField(TEXT("oldValue"), OldValue);
			Entry->SetStringField(TEXT("newValue"), Pin->DefaultValue);
			PinDefaultResults.Add(MakeShared<FJsonValueObject>(Entry));
			DefaultsSet++;
		}
	}

	// ------------------------------------------------------------------
	// Phase 4 — mark, compile, and save ONCE.
	// ------------------------------------------------------------------
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	// CLAUDE-NOTE: no separate compile step — SaveBlueprintPackage already runs an SEH-guarded
	// compile as its phase 1. Compiling here as well would compile the Blueprint twice per batch,
	// which is the exact cost this handler exists to remove.
	const bool bSaved = SaveBlueprintPackage(BP);
	const bool bCompiled = (BP->Status == BS_UpToDate);

	UE_LOG(LogTemp, Display,
		TEXT("BlueprintMCP: BuildGraph '%s' in '%s' — nodes %d/%d, connections %d/%d, defaults %d/%d, compile %s, save %s"),
		*DecodedGraphName, *BlueprintName,
		NodesCreated, NodesCreated + NodesFailed,
		ConnectionsMade, ConnectionsMade + ConnectionsFailed,
		DefaultsSet, DefaultsSet + DefaultsFailed,
		bCompiled ? TEXT("ok") : TEXT("not-up-to-date"),
		bSaved ? TEXT("ok") : TEXT("FAILED"));

	// CLAUDE-NOTE: `success` is true only when EVERY item applied. VibeUE's equivalent returns a
	// bare null on any partial failure, forcing the agent to go read the editor log to find out
	// what broke; we always return per-item results so a partial batch can be repaired precisely.
	const bool bAllOk = (NodesFailed == 0 && ConnectionsFailed == 0 && DefaultsFailed == 0);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bAllOk);
	Result->SetBoolField(TEXT("partial"), !bAllOk);
	Result->SetStringField(TEXT("blueprint"), BlueprintName);
	Result->SetStringField(TEXT("graph"), DecodedGraphName);
	Result->SetNumberField(TEXT("nodesCreated"), NodesCreated);
	Result->SetNumberField(TEXT("nodesFailed"), NodesFailed);
	Result->SetNumberField(TEXT("connectionsMade"), ConnectionsMade);
	Result->SetNumberField(TEXT("connectionsFailed"), ConnectionsFailed);
	Result->SetNumberField(TEXT("pinDefaultsSet"), DefaultsSet);
	Result->SetNumberField(TEXT("pinDefaultsFailed"), DefaultsFailed);
	Result->SetArrayField(TEXT("nodes"), NodeResults);
	Result->SetArrayField(TEXT("connections"), ConnectionResults);
	Result->SetArrayField(TEXT("pinDefaults"), PinDefaultResults);
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}
