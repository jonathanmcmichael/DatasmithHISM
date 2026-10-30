# Core instancing and Datasmith source-data plan

Date: 2026-10-02. Status: **planned, not executed**. This is a bounded plan for the open questions in the [roadmap](ROADMAP.md), not a replacement for the [import contract](IMPORT_PANEL_VALIDATION.md) or the [handoff's current status](HANDOFF.md#current-status). Dated validation records own results of actual runs.

## Outcome

Establish whether converting repeated Revit Datasmith objects into ISM/HISM output makes a large model more usable at runtime, explain why other objects remain ordinary, and identify source information worth retaining or exposing. Do not claim a performance win from import-side counts alone or treat material curation as a prerequisite.

The current UE 5.8.3 stock Datasmith scene path creates hierarchical-instanced actor elements; the plugin imports those and replaces verified HISM components with ISM components when ISM was requested. This is an implementation choice at the Datasmith scene-factory boundary, **not** a general Unreal restriction on constructing ISMs directly. Keep this path unless its measured cost or fidelity gives a reason to change it.

## Workstreams and gates

### 1. Prove the core runtime claim

**Starting point:** the [CWM ordinary/ISM/HISM comparison](Docs/Validation/2026-10-02-runtime-comparison.md) has three verified saved maps and a Win64 package, but no runtime profiling. Ordinary output has 7,101 mesh elements; ISM and HISM each have 379 groups / 2,561 instances and 4,540 ordinary elements. These are import results, not frame-time results.

1. When the shared desktop is explicitly available, confirm the imported camera works in the packaged maps or establish the same reproducible camera positions for all three variants. Do not launch the packaged app or take desktop input before that authorization.
2. Use identical resolution, rendering/scalability, power mode, camera views (interior, exterior, near and distant), and warm/steady-state capture windows. Record CPU game/render/RHI and GPU frame time, draw calls, memory, visible instance behavior, and visual differences; repeat to expose noise. Preserve raw Insights traces, settings, logs, and screenshots with a dated evidence record. Do not mix cold-cache hashing or editor import time into runtime comparisons.
3. Compare ordinary vs ISM vs HISM per view. Identify regressions as well as wins and distinguish actor/component overhead from rendering, culling, shadow, and geometry costs. State whether the CWM missing textures constrain each conclusion.
4. Repeat on representative complete ARCH/HVAC exports and a high-unique-mesh source when their inputs are available; record blocked gates honestly. Set source/target-specific budgets only after baselines exist. Nanite off/recommended/all is a later controlled comparison, not a substitute for the base instancing result.

**Gate:** a reproducible, packaged same-source comparison supports or falsifies a useful runtime benefit, with raw evidence and explicitly scoped hardware/views. If results are inconclusive or worse, diagnose the bottleneck before extending grouping behavior. This workstream also supplies the [performance acceptance row](Docs/VALIDATION.md#required-acceptance-matrix), not the full release decision.

### 2. Explain ordinary leftovers before changing grouping

**Starting point:** [group planning](Source/DatasmithHISM/Private/ConVerseDatasmithImportService.cpp) keys on the exact mesh reference, immediate parent, overrides, layer, component/visibility/shadow/mobility settings. Actors with children, unsupported transforms, unresolved assets/materials, or too few compatible siblings remain ordinary. Zero groups is a valid outcome.

1. Extend read-only Analyze reporting to account for *all* source mesh actors once: converted instances; eligible groups below the minimum; unique mesh references; parent/settings/material splits; explicit keep-ordinary decisions; and unsafe/unresolved cases. Check how many ordinary actors and components each reason could plausibly save, without assuming a group across different source meshes is safe.
2. First validate classification against existing generated fixtures and the CWM export. Test that counts partition the input, Analyze does not mutate scene assets, and report totals match the import's group/ordinary inventory. Capture a source-level breakdown for each representative model.
3. Only then rank bottlenecks. If same-geometry assets with distinct Datasmith identities dominate, propose a separately reviewed cross-asset equivalence proof covering geometry, UVs, sections, materials, collision, LODs, identity, and rollback. If parent boundaries dominate, prove attachment/world placement and source lookup can survive any proposed regrouping. Do not weaken eligibility merely to raise the instance count.

**Gate:** a reproducible breakdown explains the 4,540 CWM ordinary elements and identifies whether a safe grouping improvement is likely to matter. Any changed grouping/ownership behavior requires a contract and PlanId review, focused automation, real-source verification, and updated evidence before it can be accepted.

### 3. Audit useful Datasmith data and unresolved references

**Starting point:** the planner captures mesh-actor tags, metadata keys/values and identity candidates; the editor manifest keeps hierarchy, transforms, tags and metadata. Cookable per-component/instance lookup exposes source identifiers and a string map of metadata, but not the source tags, hierarchy or metadata property types. This does not prove the `.udatasmith` file contains any particular Revit field, or that an omitted field can be recovered from a closed-source exporter.

1. With complete source and `_Assets` sidecar, inventory scene/exporter provenance, mesh and non-mesh actors, hierarchy, mesh references, transforms, metadata key names/types and duplicate keys, tags, camera/light elements, material/texture references, and coordinate evidence. Compare Revit 2025/Twinmotion 2024 and Revit 2027 exports only when equivalent source views are supplied; record what is present, absent, or ambiguous rather than infer exporter internals.
2. Triage CWM's missing-actor metadata-reference warnings by target and source element. Quantify distinct broken references and whether they affect grouping, inspection, runtime lookup, placement, or only noncritical annotations. Keep warning counts and representative raw examples in dated evidence; do not silently discard them.
3. For valuable exported fields (for example element ID, category, family/type, level, host, phase, coordinates), trace source -> Analyze -> manifest -> cooked lookup for actual samples. State which fields already survive as raw metadata and which are inaccessible, flattened, duplicated, or missing from source. Test a proposed retention change on a small fixture and account for per-instance cooked size before expanding the runtime schema.
4. Keep geometric/photometric gaps separate: the absent joist diagonals, missing image textures and Unitless lighting cannot be reconstructed from labels or metadata. Request an authoritative corrected export, dependencies, or Revit values only for the gate they block.

**Gate:** a source-to-output field inventory and warning classification, with a prioritized list of *demonstrated* information losses and their user value. New runtime fields, metadata parsing, or reference repairs need separate scope/contract decisions; no speculative exporter workaround.

## Execution boundaries

- Start with workstream 1's prepared maps when desktop access is granted; workstreams 2 and 3 can use read-only analysis meanwhile. Do not substitute repeated headless service tests for rendered or live UI evidence.
- Use application-native Unreal tests for new code and the real build/automation procedure in [validation](Docs/VALIDATION.md) with the editor closed. Preserve existing uncommitted work, saved validation maps, and retained recovery artifacts. No shell smoke harness or forced process termination.
- Keep the [live UI re-check](HANDOFF.md#next-agent-assignment), safety contract, and broader release matrix visible as separate pending gates. No Phase 6, material-library expansion, automatic cleanup, or direct-ISM import rewrite is authorized by this plan.
- After each workstream, record evidence and limitations in a dated validation note, update the handoff's current status only after real runs, and revise the roadmap when the gate's outcome changes priorities. Stop and request a scope decision before a grouping or schema redesign.
