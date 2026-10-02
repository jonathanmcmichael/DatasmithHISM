# Opt-in Unreal Insights trace capture, 2026-10-01

## Behavior

The import panel's **Profile next import** checkbox is transient and off by default. It is consumed by the next Import/Rebuild operation, not Analyze. A successful start writes a full-duration `.utrace` under the host project's `Saved/Profiling` directory, reports its path with the import result, and stops when the operation handler completes. Capture start or stop failures are reported but do not block the import. If another trace is already active, the capture leaves it unchanged. The trace path and checkbox do not enter the normalized plan, `PlanId`, presets, or restored session settings.

The file sink is started with UE's `ExcludeTail` option, so the result is a complete trace from capture start rather than a bounded rolling snapshot. Required `cpu`, `frame`, `bookmark`, and `log` channels are enabled only when needed and restored after capture.

## Validation

- UE 5.8.3 `AdvancedHISMEditor Win64 Development` build: succeeded with the editor closed after the final code changes.
- `DatasmithHISM.OptimizedImport.TraceCaptureAndReportPath`: passed, exit 0; verified import during capture, trace path in the report, owned stop, channel restoration, nonempty output, and a second capture after the first sink closed. Host log: `Saved/Logs/TraceCaptureLatentRetry.txt`.
- `Automation RunTests DatasmithHISM`: 47 tests discovered, 47 successes, zero failures, exit 0 on the final source tree. Host log: `Saved/Logs/TraceFeatureFinalSuite.txt`.

The Unreal commandlet's SDK preflight reported optional LinuxArm64 and VisionOS SDKs unavailable; the automation run itself completed with exit code 0. No live Slate interaction or large-source import was performed for this feature. The test uses a generated small Datasmith fixture; it does not establish rendered or packaged acceptance.
