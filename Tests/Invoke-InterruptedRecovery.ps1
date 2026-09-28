param(
    [string]$Project = 'D:/Unreal/Sandbox/AdvancedHISM/AdvancedHISM.uproject',
    [string]$Editor = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe',
    [string]$RunName = ('Interruption-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
)
$ErrorActionPreference = 'Stop'
if (Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
    throw 'Close Unreal Editor before running disposable acceptance fixtures.'
}
if ($RunName -notmatch '^[A-Za-z0-9_-]+$') { throw 'RunName must be a simple unique folder name.' }
$projectFile = (Resolve-Path -LiteralPath $Project).Path
$projectRoot = Split-Path -Parent $projectFile
$editorFile = (Resolve-Path -LiteralPath $Editor).Path
$evidence = Join-Path $projectRoot ('Saved/Phase2Acceptance/' + $RunName)
if (Test-Path -LiteralPath $evidence) { throw "Evidence directory already exists: $evidence" }
New-Item -ItemType Directory -Path $evidence | Out-Null

function Start-Fixture([string]$Test, [string]$LogName) {
    $arguments = @(
        ('"' + $projectFile + '"'),
        ('"-ExecCmds=Automation RunTests ' + $Test + '; Quit"'),
        '-unattended', '-nopause', '-nosplash', '-NullRHI',
        ('"-ConVerseInterruptEvidence=' + $evidence + '"'),
        ('"-abslog=' + (Join-Path $evidence ($LogName + '.txt')) + '"')
    )
    Start-Process -FilePath $editorFile -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $evidence ($LogName + '-stdout.txt')) `
        -RedirectStandardError (Join-Path $evidence ($LogName + '-stderr.txt'))
}

$fixtureProcess = Start-Fixture 'ConVerseAcceptance.InterruptedProcessFixture' 'interrupted'
$launchedAt = $fixtureProcess.StartTime
$readyFile = Join-Path $evidence 'ready.json'
$deadline = (Get-Date).AddSeconds(180)
while (!(Test-Path -LiteralPath $readyFile) -and !$fixtureProcess.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 200
}
if (!(Test-Path -LiteralPath $readyFile)) {
    throw "Disposable process did not reach its checkpoint. PID=$($fixtureProcess.Id). See $evidence"
}
$ready = Get-Content -LiteralPath $readyFile -Raw | ConvertFrom-Json
if ($ready.Terminal -or !$ready.RecoveryRequired -or $ready.Stage -ne 9 -or !$ready.ObservedObjects.Count) {
    throw 'The recorded checkpoint is not verified, uncommitted, and populated.'
}
$liveProcess = Get-Process -Id $fixtureProcess.Id -ErrorAction Stop
$processInfo = Get-CimInstance Win32_Process -Filter "ProcessId=$($fixtureProcess.Id)"
if ($liveProcess.StartTime -ne $launchedAt -or $processInfo.ExecutablePath -ne $editorFile `
    -or !$processInfo.CommandLine.Contains($evidence)) {
    throw 'Process identity changed; refusing termination.'
}
# Only the process returned by Start-Process above is ever terminated.
Stop-Process -InputObject $fixtureProcess -Force
$fixtureProcess.WaitForExit()

$packageNames = @($ready.Map) + @($ready.ObservedObjects | ForEach-Object { ($_ -split '\.')[0] })
$files = foreach ($packageName in ($packageNames | Sort-Object -Unique)) {
    if (!$packageName.StartsWith('/Game/__ConVersePersistence/')) { throw "Unexpected package: $packageName" }
    $extension = if ($packageName -eq $ready.Map) { '.umap' } else { '.uasset' }
    $path = Join-Path $projectRoot ('Content/' + $packageName.Substring(6) + $extension)
    if (Test-Path -LiteralPath $path) { (Resolve-Path -LiteralPath $path).Path }
}
$files += $ready.JournalFile
$before = @($files | Sort-Object -Unique | ForEach-Object {
    [pscustomobject]@{ Path = $_; SHA256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
})
$before | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'before-restart.json')
$probe = Start-Fixture 'ConVerseAcceptance.InterruptedRecoveryProbe' 'recovery-process'
$probe.WaitForExit()
$unchanged = $true
foreach ($entry in $before) {
    if (!(Test-Path -LiteralPath $entry.Path) -or (Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256).Hash -ne $entry.SHA256) {
        $unchanged = $false
    }
}
$result = [ordered]@{
    InterruptedProcessId = $fixtureProcess.Id
    InterruptedProcessStarted = $launchedAt.ToUniversalTime().ToString('o')
    CheckpointStage = $ready.Stage
    Session = $ready.Session
    Map = $ready.Map
    OwnedFolder = $ready.OwnedFolder
    ObservedObjectCount = $ready.ObservedObjects.Count
    PreservedFileCount = $before.Count
    RecoveryExitCode = $probe.ExitCode
    AllRecordedFilesUnchanged = $unchanged
    EvidenceDirectory = $evidence
}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'result.json')
$result | ConvertTo-Json
if ($probe.ExitCode -ne 0 -or !$unchanged) { throw 'Interrupted recovery acceptance failed.' }
