[CmdletBinding()]
param(
    [ValidateSet("ComputerCpp", "computer.cpp", "all")]
    [string]$Target = "ComputerCpp",

    [string]$BuildDir = "build/windows-gui"
)

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $projectDir $BuildDir
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

function Enter-ComputerCppDeveloperShell {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "Visual Studio Build Tools were not found. Install the Desktop development with C++ workload."
    }

    $vsInstallPath = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath

    if (-not $vsInstallPath) {
        throw "The Visual Studio C++ build tools were not found. Install the Desktop development with C++ workload."
    }

    $devShellModule = Join-Path $vsInstallPath "Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
    Import-Module $devShellModule
    Enter-VsDevShell `
        -VsInstallPath $vsInstallPath `
        -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
}

function Find-VcpkgToolchain {
    $candidates = @()
    if ($env:VCPKG_ROOT) {
        $candidates += Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
    }
    $candidates += Join-Path $projectDir "build/tools/vcpkg/scripts/buildsystems/vcpkg.cmake"

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    throw "vcpkg was not found. Set VCPKG_ROOT or install it at build/tools/vcpkg."
}

Enter-ComputerCppDeveloperShell

$cmakeCache = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $cmakeCache)) {
    $toolchain = Find-VcpkgToolchain
    & cmake `
        -S $projectDir `
        -B $BuildDir `
        -G Ninja `
        -DCMAKE_BUILD_TYPE=Debug `
        -DBUILD_TESTING=ON `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed with exit code $LASTEXITCODE."
    }
}

& cmake --build $BuildDir --target $Target --config Debug
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}
