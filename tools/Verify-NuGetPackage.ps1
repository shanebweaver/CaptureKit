[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackagePath,

    [Parameter(Mandatory)]
    [string]$DumpBinPath,

    [ValidateSet('x64', 'ARM64')]
    [string[]]$Platforms = @('x64', 'ARM64')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Compression.FileSystem

$resolvedPackage = (Resolve-Path -LiteralPath $PackagePath).Path
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("CaptureKit-package-verification-" + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $temporaryRoot

try {
    $archive = [System.IO.Compression.ZipFile]::OpenRead($resolvedPackage)
    try {
        $entryNames = @($archive.Entries | ForEach-Object FullName)
        if ($entryNames -notcontains 'buildTransitive/CaptureKit.Windows.targets') {
            throw "'$resolvedPackage' does not contain its build-transitive native asset targets."
        }

        foreach ($platform in $Platforms) {
            $runtimeIdentifier = if ($platform -eq 'ARM64') { 'win-arm64' } else { 'win-x64' }
            $destination = Join-Path $temporaryRoot "native\Release\$platform"
            $null = New-Item -ItemType Directory -Force -Path $destination

            foreach ($fileName in @(
                'CaptureKit.Windows.Native.dll',
                'CaptureKit.Windows.Native.pdb',
                'CaptureKit.Windows.Native.Screenshot.dll',
                'CaptureKit.Windows.Native.Screenshot.pdb'
            )) {
                $entryName = "runtimes/$runtimeIdentifier/native/$fileName"
                $entry = $archive.GetEntry($entryName)
                if (-not $entry) {
                    throw "'$resolvedPackage' does not contain '$entryName'."
                }

                if ($fileName.EndsWith('.dll', [StringComparison]::OrdinalIgnoreCase)) {
                    [System.IO.Compression.ZipFileExtensions]::ExtractToFile(
                        $entry,
                        (Join-Path $destination $fileName),
                        $true)
                }
            }
        }
    }
    finally {
        $archive.Dispose()
    }

    & (Join-Path $PSScriptRoot 'Verify-NativeBinaries.ps1') `
        -DumpBinPath $DumpBinPath `
        -ArtifactRoot $temporaryRoot `
        -Configuration Release `
        -Platforms $Platforms

    Write-Host "Verified NuGet runtime assets in '$resolvedPackage'."
}
finally {
    $resolvedTemporaryRoot = [System.IO.Path]::GetFullPath($temporaryRoot)
    $resolvedSystemTemp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($resolvedTemporaryRoot.StartsWith($resolvedSystemTemp, [StringComparison]::OrdinalIgnoreCase) `
        -and (Split-Path -Leaf $resolvedTemporaryRoot).StartsWith('CaptureKit-package-verification-', [StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
