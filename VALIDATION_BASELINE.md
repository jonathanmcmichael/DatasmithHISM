# DatasmithHISM validation baseline

Current baseline: **2026-09-27, Unreal Engine 5.8.3**. The project root is not a Git working tree; the plugin directory is. The earlier 2026-09-23 baseline is [archived](Docs/History/2026-09-26/VALIDATION_BASELINE.md).

[Current fixes and executed evidence](Docs/Validation/2026-09-27-live-ui.md) record live UI results, a successful build and 32 tests with 55 source hashes. The earlier [closeout checks](Docs/Validation/2026-09-27-closeout.md) remain a historical documentation-only record.

| Check | Recorded result |
|---|---|
| Editor build | Passed |
| Full DatasmithHISM automation | 33/33 passed; process exit 0; no controller errors |
| Save / initial save-as / fresh-process reopen | Passed for generated validation content |
| Read-only owning-map save | Correctly failed with exit 1; map unchanged; attributes restored |
| Named-map copy and manual-drift persistence | Identity-changing copy refused; original re-verifies; saving drift does not accept a new baseline |
| Real read-only asset / simulated map write-capacity failure | Both report incomplete save; affected bytes unchanged; retry succeeds |
| Actual pre-commit process interruption / restart | Known paths reported; 4 saved evidence files unchanged |
| Windows Development cook/archive | Passed |
| Packaged NullRHI and DX12 startup/runtime | Passed source/mesh/material/light/IES/collision smoke checks |
| Full release acceptance | Incomplete |

Automation is runnable; optional-platform SDK messages did not block the suite. Optimizer-aware replacement and rollback are implemented and exercised. Representative source fixtures now exist, but the extracted joist payload lacks webbing, full exports have missing textures, and Revit photometric calibration remains unresolved.

Use [validation procedure and required scenarios](Docs/VALIDATION.md), [earlier Phase 2 evidence](Docs/Validation/2026-09-26-phase2.md), [current source hashes/results](Docs/Validation/2026-09-27-preset-cancellation-evidence.json), [earlier package evidence](Docs/Validation/2026-09-26.md), and the [fixture index](Tests/Fixtures/README.md). This pointer does not create new test evidence.
