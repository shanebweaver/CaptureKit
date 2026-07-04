[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'ARM64')]
    [string[]]$Platforms = @('x64', 'ARM64'),

    [switch]$SkipNative,
    [switch]$SkipNativeTests,
    [switch]$SkipManagedTests,
    [switch]$SkipPack,

    [ValidateSet('quiet', 'minimal', 'normal', 'detailed', 'diagnostic')]
    [string]$Verbosity = 'minimal',

    [string]$MSBuildPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-MSBuildPath {
    param(
        [string]$ExplicitPath
    )

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }

        throw "MSBuild was not found at '$ExplicitPath'."
    }

    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswherePath = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'

    if (Test-Path -LiteralPath $vswherePath -PathType Leaf) {
        $queries = @(
            @('-latest', '-products', '*', '-requires', 'Microsoft.Component.MSBuild', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-find', 'MSBuild\Current\Bin\MSBuild.exe'),
            @('-latest', '-products', '*', '-requires', 'Microsoft.Component.MSBuild', '-find', 'MSBuild\Current\Bin\MSBuild.exe')
        )

        foreach ($query in $queries) {
            $matches = & $vswherePath @query
            foreach ($match in $matches) {
                if ($match -and (Test-Path -LiteralPath $match -PathType Leaf)) {
                    return $match
                }
            }
        }
    }

    $programFiles = [Environment]::GetFolderPath('ProgramFiles')
    $commonInstallRoots = @(
        (Join-Path $programFiles 'Microsoft Visual Studio\18'),
        (Join-Path $programFiles 'Microsoft Visual Studio\2022')
    )
    $editions = @('Community', 'Professional', 'Enterprise', 'BuildTools', 'Preview')

    foreach ($root in $commonInstallRoots) {
        foreach ($edition in $editions) {
            $candidate = Join-Path $root "$edition\MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    throw @"
Could not find Visual Studio MSBuild.

Install Visual Studio with the "Desktop development with C++" workload, or pass -MSBuildPath with the full path to MSBuild.exe.
This repo contains C++ projects, so dotnet build alone cannot build the native layer.
"@
}

function Invoke-Process {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,

        [AllowEmptyCollection()]
        [string[]]$Arguments = @()
    )

    Write-Host "> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE."
    }
}

function Resolve-VSTestPath {
    param(
        [string]$MSBuild
    )

    $msbuildDirectory = Split-Path -Path $MSBuild -Parent
    $vsRoot = Split-Path -Path (Split-Path -Path (Split-Path -Path $msbuildDirectory -Parent) -Parent) -Parent
    $candidate = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return $candidate
    }

    return $null
}

$repoRoot = $PSScriptRoot
$msbuild = Resolve-MSBuildPath -ExplicitPath $MSBuildPath
$packageOutput = Join-Path $repoRoot 'artifacts\packages'
$solutionDirProperty = "/p:SolutionDir=$repoRoot\"

Write-Host "Using MSBuild: $msbuild"

$vcpkgBootstrap = Join-Path $repoRoot 'vcpkg\bootstrap-vcpkg.bat'
$vcpkgExe = Join-Path $repoRoot 'vcpkg\vcpkg.exe'
if ((Test-Path -LiteralPath $vcpkgBootstrap -PathType Leaf) -and -not (Test-Path -LiteralPath $vcpkgExe -PathType Leaf)) {
    Invoke-Process -FilePath $vcpkgBootstrap -Arguments @()
}

if (-not $SkipNative) {
    foreach ($platform in $Platforms) {
        Invoke-Process -FilePath $msbuild -Arguments @(
            (Join-Path $repoRoot 'src\CaptureKit.Windows.Native\CaptureKit.Windows.Native.vcxproj'),
            '/restore',
            '/m',
            '/nologo',
            "/v:$Verbosity",
            "/p:Configuration=$Configuration",
            "/p:Platform=$platform",
            $solutionDirProperty
        )
    }
}

Invoke-Process -FilePath 'dotnet' -Arguments @(
    'build',
    (Join-Path $repoRoot 'src\CaptureKit.Abstractions\CaptureKit.Abstractions.csproj'),
    '-c',
    $Configuration
)

Invoke-Process -FilePath 'dotnet' -Arguments @(
    'build',
    (Join-Path $repoRoot 'src\CaptureKit.Windows\CaptureKit.Windows.csproj'),
    '-c',
    $Configuration
)

if (-not $SkipNativeTests -and -not $SkipNative -and $Platforms -contains 'x64') {
    Invoke-Process -FilePath $msbuild -Arguments @(
        (Join-Path $repoRoot 'tests\CaptureKit.Windows.Native.Tests\CaptureKit.Windows.Native.Tests.vcxproj'),
        '/restore',
        '/m',
        '/nologo',
        "/v:$Verbosity",
        "/p:Configuration=$Configuration",
        '/p:Platform=x64',
        $solutionDirProperty
    )

    $vsTest = Resolve-VSTestPath -MSBuild $msbuild
    if (-not $vsTest) {
        throw 'Could not find vstest.console.exe for native test execution.'
    }

    $nativeTestDll = Get-ChildItem -Path $repoRoot -Recurse -File -Filter 'CaptureKit.Windows.Native.Tests.dll' |
        Where-Object { $_.FullName -match "\\x64\\$Configuration\\" } |
        Select-Object -First 1

    if (-not $nativeTestDll) {
        throw 'Could not find CaptureKit.Windows.Native.Tests.dll after building native tests.'
    }

    Invoke-Process -FilePath $vsTest -Arguments @($nativeTestDll.FullName, '/Platform:x64')
}

if (-not $SkipManagedTests) {
    Invoke-Process -FilePath 'dotnet' -Arguments @(
        'test',
        (Join-Path $repoRoot 'tests\CaptureKit.Windows.Tests\CaptureKit.Windows.Tests.csproj'),
        '-c',
        $Configuration
    )
}

if (-not $SkipPack) {
    New-Item -ItemType Directory -Force -Path $packageOutput | Out-Null
    Invoke-Process -FilePath 'dotnet' -Arguments @(
        'pack',
        (Join-Path $repoRoot 'src\CaptureKit.Abstractions\CaptureKit.Abstractions.csproj'),
        '-c',
        $Configuration,
        '--no-build',
        '-o',
        $packageOutput
    )

    Invoke-Process -FilePath 'dotnet' -Arguments @(
        'pack',
        (Join-Path $repoRoot 'src\CaptureKit.Windows\CaptureKit.Windows.csproj'),
        '-c',
        $Configuration,
        '--no-build',
        '-o',
        $packageOutput
    )
}
