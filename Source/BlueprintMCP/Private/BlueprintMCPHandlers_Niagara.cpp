#include "BlueprintMCPServer.h"

// CLAUDE-NOTE: Niagara asset + stack authoring handlers (Tier 1 + Tier 2 of the
// niagara_extension_plan). Tier 3 (custom UNiagaraNode graph authoring) lives
// in a future BlueprintMCPHandlers_NiagaraGraph.cpp if ever needed.
//
// This file is intentionally minimal in the pre-flight commit — only the module
// dependency probe is here. Handlers land in subsequent commits, one tier at a
// time, mirroring BlueprintMCPHandlers_MaterialMutation.cpp's structure.

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"

#if WITH_EDITOR
#include "NiagaraEditorModule.h"
#endif

// Force a referenced symbol from each Niagara module so the linker actually
// pulls them in even though no handler has been written yet. Without this the
// .cpp produces a translation unit but no external references, and a bad
// module dep wouldn't surface until the first real handler is added.
namespace
{
	void NiagaraModuleProbe()
	{
		(void)UNiagaraSystem::StaticClass();
		(void)UNiagaraEmitter::StaticClass();
	}
}
