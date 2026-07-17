[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$DumpBinPath,

    [Parameter(Mandatory)]
    [string]$ArtifactRoot,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'ARM64')]
    [string[]]$Platforms = @('x64', 'ARM64')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$requiredScreenshotExports = @(
    'CaptureMonitorScreenshot',
    'CaptureAllMonitorsScreenshot',
    'GetScreenshotInfo',
    'CopyScreenshotPixels',
    'SaveScreenshotToPng',
    'FreeScreenshot',
    'CombineScreenshots'
)

$requiredRecordingExports = @(
    'StartScreenRecording',
    'PauseScreenRecording',
    'ResumeScreenRecording',
    'StopScreenRecording',
    'SetScreenRecordingAudioEnabled',
    'SetScreenRecordingAudioInputSource',
    'SetScreenRecordingAudioInputVolume',
    'RegisterVideoFrameCallback',
    'RegisterAudioSampleCallback',
    'StartAudioRecording',
    'PauseAudioRecording',
    'ResumeAudioRecording',
    'StopAudioRecording',
    'SetAudioRecordingEnabled',
    'SetAudioRecordingInputSource',
    'SetAudioRecordingInputVolume',
    'RegisterAudioRecordingSampleCallback',
    'ConvertTextureToPixelBuffer'
)

$forbiddenScreenshotDependencies = @(
    'MFPlat.dll',
    'MFReadWrite.dll',
    'MSVCP140.dll',
    'VCRUNTIME140.dll',
    'VCRUNTIME140_1.dll'
)

function Invoke-DumpBin {
    param(
        [Parameter(Mandatory)]
        [string]$Option,

        [Parameter(Mandatory)]
        [string]$Path
    )

    $output = & $DumpBinPath $Option $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for '$Path' with exit code $LASTEXITCODE.`n$($output -join [Environment]::NewLine)"
    }

    return $output -join [Environment]::NewLine
}

foreach ($platform in $Platforms) {
    $nativeDirectory = Join-Path $ArtifactRoot "native\$Configuration\$platform"
    $screenshotDll = Join-Path $nativeDirectory 'CaptureKit.Windows.Native.Screenshot.dll'
    $recordingDll = Join-Path $nativeDirectory 'CaptureKit.Windows.Native.Recording.dll'
    $legacyDll = Join-Path $nativeDirectory 'CaptureKit.Windows.Native.dll'

    if (Test-Path -LiteralPath $legacyDll -PathType Leaf) {
        throw "Legacy native binary '$legacyDll' is still present; recording assets must use the descriptive name."
    }

    foreach ($path in @($screenshotDll, $recordingDll)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required native binary was not produced: '$path'."
        }
    }

    $headers = Invoke-DumpBin -Option '/HEADERS' -Path $screenshotDll
    $expectedMachine = if ($platform -eq 'ARM64') { 'AA64 machine \(ARM64\)' } else { '8664 machine \(x64\)' }
    if ($headers -notmatch $expectedMachine) {
        throw "'$screenshotDll' does not have the expected $platform machine type."
    }

    $exports = Invoke-DumpBin -Option '/EXPORTS' -Path $screenshotDll
    foreach ($export in $requiredScreenshotExports) {
        if ($exports -notmatch "(?m)\b$([Regex]::Escape($export))\b") {
            throw "'$screenshotDll' does not export '$export'."
        }
    }

    $dependencies = Invoke-DumpBin -Option '/DEPENDENTS' -Path $screenshotDll
    foreach ($dependency in $forbiddenScreenshotDependencies) {
        if ($dependencies -match "(?im)^\s*$([Regex]::Escape($dependency))\s*$") {
            throw "'$screenshotDll' must not depend on '$dependency'."
        }
    }

    $recordingExports = Invoke-DumpBin -Option '/EXPORTS' -Path $recordingDll
    foreach ($export in $requiredRecordingExports) {
        if ($recordingExports -notmatch "(?m)\b$([Regex]::Escape($export))\b") {
            throw "'$recordingDll' does not export required recording function '$export'."
        }
    }

    foreach ($export in $requiredScreenshotExports) {
        if ($recordingExports -match "(?m)\b$([Regex]::Escape($export))\b") {
            throw "'$recordingDll' still exports '$export'; screenshot code must remain isolated from recording dependencies."
        }
    }

    Write-Host "Verified native dependency isolation, exports, and machine type for $platform."
}
