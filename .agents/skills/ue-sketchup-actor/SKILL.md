---
name: ue-sketchup-actor
description: Convert an already imported SketchUp or Datasmith building into one reusable Unreal Actor Blueprint whose separate placements remain individual Static Mesh Components, with a parent component for group transforms.
---

# Convert an Imported Building to One Actor

Use this when a user wants a SketchUp/Datasmith building to behave as one placed Actor while retaining individually editable static mesh parts. Assume UE 5.8, the project MCP tools, and the target import are already available unless the user says otherwise. Do not repeat MCP installation or connection setup.

## Desired result

- A reusable Actor Blueprint in the requested content folder.
- One Static Mesh Component for every intended source mesh placement; repeated placements may reference the same Static Mesh asset but must remain separate components.
- A named Scene Component such as `BuildingMeshGroup` under the actor root, with the mesh components attached beneath it. This provides a convenient group transform inside the Blueprint.
- Preserve the imported layout, per-part transforms, material overrides, visibility, and relevant render properties.
- Keep the source import intact unless the user explicitly asks to replace or remove it.

## Workflow

1. Read the current project instructions and inspect Git status before editing. Preserve unrelated changes and do not stage them.
2. Discover the available Unreal MCP toolsets and schemas for each operation. Follow the project's UE MCP guide when present. UE MCP calls are serialized on the editor game thread; make one editor mutation at a time, or use the Programmatic tool only after reading its execution-environment instructions and the relevant input/output schemas.
3. Confirm the loaded level and identify the intended source Actor set. Use exact asset references, Datasmith dependencies, Outliner folders, tags, or an unambiguous name filter. Exclude unrelated actors explicitly. Stop and ask the user if the candidate set is ambiguous; do not infer that every actor in a test level belongs to the building.
4. Inspect existing Blueprints and dependencies first. Reuse an appropriate asset only if its component count and per-component data are correct. Otherwise create a new Blueprint or duplicate an appropriate base into a new, clearly named asset. Do not overwrite an existing Blueprint blindly.
5. Build a one-to-one mapping from intended placed source meshes to Blueprint mesh components. Match by the exact Static Mesh asset path, and also account for repeated placements rather than deduplicating them. Fail with a clear error if any source placement is unmatched, matched more than once, or has no valid mesh.
6. For each placement, copy its world transform into the new Blueprint component's local transform relative to an identity root/group. Preserve the Static Mesh reference, material overrides, visibility, cast-shadow state, and other properties that differ meaningfully from defaults. Choose component mobility according to the user's runtime needs: use `Movable` when the assembled building is meant to move; otherwise preserve static-building behavior where valid. Do not silently change collision or physics behavior.
7. Add a named Scene Component under the actor root. Attach all mesh components beneath it while its transform is identity. Keep each mesh component's local transform unchanged during reparenting. This lets users adjust all building geometry together inside the Blueprint while retaining per-part editability.
8. Compile the Blueprint, inspect compile errors/warnings, save only the exact new or modified asset paths, and query dirty state. Never save all dirty project assets as a convenience.
9. Verify by reading back the Blueprint hierarchy and comparing every component's mesh path, relative transform, material overrides, and relevant properties against the source placements. Confirm the component count and mapping counts agree, with zero unexpected/missing components. If possible, place or inspect one instance and confirm the group component controls the complete building.
10. Commit the implementation with a conventional prefix if the project's AGENTS.md requires per-step commits. Stage only the Blueprint and genuinely required new dependencies; keep user changes and unrelated imported level data out of the commit. Never push unless the user authorizes it.
11. Tell the user the asset path, component count, hierarchy/group-transform behavior, any remaining limitation (especially pivot placement), commit ID, and a directly executable editor acceptance procedure.

## Transform and pivot behavior

The actor root determines the placed Actor's transform and pivot. The named group Scene Component is an editable transform for all mesh content inside the Blueprint, but moving that component does not relocate the actor root pivot. To establish a precise world-space pivot, select the intended pivot point first, calculate a consistent offset for all child meshes, and preserve the building's world appearance. Do not guess a pivot point. Report the distinction clearly when the user has only requested a group adjustment control.

When parenting components, prefer preserving-world reparenting if the MCP API provides it. If the API does not make preservation behavior explicit, read child transforms before parenting, use an identity parent, then compare all local transforms afterward; stop and correct any mismatch before saving.

## Scope and failure handling

- Never use mesh merge operations for this workflow: they destroy the requested per-part mesh boundaries.
- Keep source imported actors and assets unless replacement/removal was explicitly requested.
- Avoid silent fallbacks. If the selected source set, asset correspondence, transform conversion, property access, compile, save, or verification fails, report the exact mismatch and leave the source intact.
- For large scenes, use a bounded batch/script to reduce MCP round trips, but validate counts and every mapped record. Prefer clear errors over best-effort continuation.
- Do not add project-specific C++ or documentation unless requested; this skill is about operating on imported assets through the existing editor MCP workflow.
