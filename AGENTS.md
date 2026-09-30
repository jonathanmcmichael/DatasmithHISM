# agent guidance

tell the user when it's time to start a new thread. If the context window is close to full or token usage is getting high per entry

ask if the user wants to use lower reasoning agents if the current model or effor level is determited to be excessive

if the user says that this thread is a orchestrator thread, delegate tasks to other agents that use less tokens that are apropriate for the reasoning and task level.

Do not use powershell scripts to do smoke tests

## Endpoint security / EDR safety

This repository is developed on a corporate-managed Windows endpoint. Endpoint security restrictions are a hard constraint.

### Do not create or execute security-sensitive automation

Unless the user explicitly requests it after being told what will run, agents must **not**:

- Create, execute, or dynamically generate PowerShell scripts (`.ps1`) for testing, orchestration, smoke tests, process management, recovery testing, or build automation.
- Use `powershell.exe`, `pwsh.exe`, `cmd.exe /c`, `Invoke-Expression`, `iex`, `EncodedCommand`, Base64-encoded commands, dynamically constructed shell commands, or command obfuscation.
- Launch processes hidden, detached, minimized, or with suppressed windows.
- Force-kill, suspend, inject into, inspect memory of, or manipulate unrelated processes.
- Use `Stop-Process -Force`, `taskkill /F`, WMI/CIM process control, or equivalent process-management mechanisms.
- Create or modify scheduled tasks, Windows services, startup items, Run/RunOnce registry keys, WMI persistence, login scripts, shell extensions, or other OS persistence mechanisms.
- Modify Windows Defender, antivirus, firewall, SmartScreen, execution policy, AMSI, application control, certificates, security exclusions, or other endpoint-security settings.
- Download and execute code, installers, binaries, scripts, or tools as part of validation.
- Use offensive-security, red-team, credential-access, persistence, lateral-movement, evasion, or penetration-testing utilities, even for benign testing.
- Attempt to bypass or work around a command blocked by antivirus, EDR, application control, permissions, or corporate policy.

### Prefer application-native validation

Use the narrowest application-native mechanism available.

For Unreal Engine work, prefer in this order:

1. Existing C++ automation tests.
2. Unreal Automation Framework tests.
3. Unreal commandlets invoked directly with documented arguments.
4. Normal Unreal Editor build/test workflows.
5. Manual validation instructions for the user.

Do not introduce an external shell harness merely to automate a test that can be expressed inside Unreal.

Tests involving restart, save/reopen, recovery, persistence, crash handling, or interrupted operations should be implemented inside the application/test framework where possible. Do not simulate interruption by externally force-terminating processes.

### Shell-command rules

When a shell command is genuinely necessary:

- Prefer a direct executable invocation with literal arguments.
- Keep commands short, transparent, and auditable.
- Do not compose executable command strings dynamically.
- Do not use hidden windows or background process tricks.
- Do not use shell pipelines for process discovery or control.
- Do not execute commands whose security implications are unclear.
- Show the exact security-sensitive command to the user before running it when it performs process control, system configuration, package installation, or other machine-level changes.

### Corporate security blocks are stop conditions

If Windows Defender, EDR, antivirus, application control, PowerShell, or another corporate security mechanism blocks or flags an operation:

**STOP immediately.**

Do not retry with altered syntax.
Do not rename the file.
Do not encode or obfuscate the command.
Do not switch shells to bypass the block.
Do not disable security controls.
Do not add exclusions.
Do not attempt an equivalent workaround that performs the same blocked behavior.

Instead:

1. Preserve the error/detection information.
2. Report the exact operation that triggered it.
3. Explain what the operation was intended to accomplish.
4. Replace it with an application-native or manual validation approach.
5. Leave endpoint-security investigation to IT/security.

### Process safety

Agents must assume all running applications may contain unsaved user work.

Do not terminate existing user processes.

If a test requires exclusive access, detect the condition and stop with instructions for the user rather than terminating anything.

A test-created child process should normally be allowed to exit naturally. External forced termination should not be added as an automated testing strategy.

### Repository policy

Do not commit automation whose primary purpose is:

- external process interruption;
- Windows persistence testing;
- endpoint-security testing;
- antivirus/EDR testing;
- shell-based process supervision;
- security-control detection or bypass.

If such behavior becomes genuinely necessary for product requirements, document the requirement first and require explicit human review before implementation.

### Terminology

Project terms such as **persistence**, **recovery**, **interruption**, and **smoke test** refer to application behavior unless explicitly stated otherwise.

Avoid filenames or tooling that could unnecessarily resemble security-testing utilities when a clearer application-specific name is available. For example, prefer names such as:

`ConVerseSaveReopenAutomation`

over generic names such as:

`Invoke-PersistenceSmoke.ps1`

### When uncertain

When an approach could reasonably resemble malware, persistence tooling, process manipulation, security testing, or endpoint evasion:

**Do not execute it. Use a safer application-native approach or ask the user first.**



Updated 2026-09-27 after preset/cancellation implementation and automation. Read the project-level [AGENTS.md](../../AGENTS.md), [handoff](HANDOFF.md), [current next steps](NEXT_STEPS.md), and [execution ledger](ROADMAP_EXECUTION.md) before new work. This is a C++ UE **5.8.3** plugin, with editor and runtime modules; it is not a .NET application.

## Scope and authority

The primary tracked importer is governed by [IMPORT_PANEL_VALIDATION.md](IMPORT_PANEL_VALIDATION.md), including its numbered amendments. Read it before changing that path. Revise the contract in the same change when a guarantee changes. [ADRs](Docs/ADR/README.md) explain decisions; [PLAN.md](PLAN.md) defines accepted scope; historical plans are not an active queue.

| Path | Purpose |
|---|---|
| `Source/DatasmithHISM/Private/ConVerseDatasmithImportService.cpp` | Tracked ownership, planning, import, verification, replacement, rollback, and commit |
| `Source/DatasmithHISM/Private/ConVerseImportProcessing.cpp` | Mesh/light/material policies and tracked state |
| `Source/DatasmithHISM/Private/ConVerseImportPersistence.cpp` | Presets, explicit save, attempt recovery diagnostics |
| `Source/DatasmithHISM/Private/ConVerseMaterialReview.cpp` | Review and exact appearance approvals |
| `Source/DatasmithHISM/Private/ConVerseHISMUtils.cpp` | Separate legacy selection conversion |
| `Source/DatasmithHISMRuntime/` | Cookable identity/lookup; no editor dependencies |

## Invariants

- Resolve source support through enabled translators; do not add an extension allowlist.
- Sources stay read-only. Sidecar fingerprints warn and stay outside PlanId; normalized output settings, tessellation, exact exceptions, and active approved mappings belong in it.
- Apply translator tessellation before loading. Assign its fields individually so engine-owned defaults survive. Clamp in the service, preserving zero max-edge-length as unconstrained.
- Keep ownership/commit/supersede/rollback decisions centralized. Ambiguous ownership, future schemas, and quarantined sessions block replacement.
- Keep stock reimport refused by the handler registered at default factory priority plus 100. Its real `FReimportManager` dispatch test must stay real-dispatch coverage.
- Older manifests never implicitly acquire new verification coverage. New records use schema 2, tracked-state version 1 and source-inventory version 1.
- Modifying a warning threshold or opening/restoring settings must not rebuild. Explicit rebuild does not itself authorize discarding tracked edits.
- Apply approved material mappings before mesh compatibility checks. External project targets never become attempt-owned. Unitless lighting remains unresolved without authoritative calibration.
- Preserve attachment/transform roots and component world placement. Source element names and metadata carry identity; sanitized labels do not.
- Already-saved map Save As changes actor GUIDs and is refused before commandlet import/copy. Copied tags do not transfer ownership. Keep the native-copy service refusal and initial unnamed-map rebinding proof distinct.

## Legacy boundary

Legacy tools have no tracked manifest or session rollback. Preserve `ConVerseManagedHISM`, `ConVerseManagedFamilyType`, and existing Blueprint compatibility. `CreateISMsFromSelection` is canonical; the old `CreateHISMsFromSelection` alias remains deprecated.

Managed grouping now includes component descriptor settings and a mesh-origin discriminator; material overrides and component transforms are copied. Keep behavior-payload rejection, instance-add result checks, and below-threshold cleanup. Do not reintroduce the resolved gaps from historical reviews.

Transactions belong at the library boundary so Dataprep can own its transaction. Keep Dataprep/headless paths free of interactive dialogs. Dedupe confirmation precedes reference replacement, and external/uncertain references skip deletion. See [legacy tools](Docs/LEGACY_TOOLS.md).

## Build and evidence

Run the [real build and automation commands](Docs/VALIDATION.md) with the editor closed. Do not kill the user's editor. The current recorded baseline is 33 passing automation tests, including rollback, manual edits, material invalidation, light thresholds, legacy placement/settings, persistence/recovery, first naming of an untitled level, and panel session restore. The explicit process-interruption fixture runs separately and may terminate only its own specifically launched disposable process. Optional-platform SDK noise did not block automation. IntelliSense cannot reliably resolve this Unreal project and is not build evidence.

Preserve existing uncommitted work. The Git root is this plugin directory; the host project root is not a Git checkout. All execution logs are under the host project's `Saved`, not this plugin's `Saved`.

The latest executed build and 32-test suite are dated 2026-09-27; [current evidence](Docs/Validation/2026-09-27-live-ui.md) records live UI results and all 55 source hashes. The earlier closeout, preset/cancellation and persistence snapshots retain their dates. Native interaction works through the guarded `NativeUI.ps1` helper (see the handoff). The next task is a live re-check of the two automation-verified fixes, then the remaining checklist rows; keep pending status where interaction or inputs are unavailable. Do not repeat successful service tests as a substitute for UI evidence. Generated interrupted sessions are retained deliberately; review the handoff before cleanup.

When adding world-scanning tests, snapshot/diff pre-existing objects because the world persists between tests. Prove safety guards can detect faults through their real entry paths. Update handoff with exact evidence and flag unbuilt/unverified code. Builds and synthetic tests never establish rendered, full-model, or packaged acceptance by themselves.

## Style and documentation

Follow Unreal types/prefixes, tabs in C++, `TEXT()` and `LOCTEXT`, and the `LogConVerseOptimizedImport` category. Keep runtime dependencies limited to runtime modules. Prefer symbol links over fragile line counts in architecture docs.

Update the [documentation index](Docs/README.md), relevant ADR/contract, [ledger](ROADMAP_EXECUTION.md), and dated evidence when behavior changes. The [journal](JOURNAL.md) is chronological history; its old claims do not override newer evidence. Do not edit user memory as part of repository documentation work.
