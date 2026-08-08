[CmdletBinding()]
param(
    [ValidateSet("ComputerCpp", "computer.cpp", "all")]
    [string]$Target = "ComputerCpp",

    [string]$BuildDir = "build/windows-gui",

    [string]$CodeSigningSubject = "CN=Gobii AI computer.cpp",

    [switch]$NoCodeSign
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

function Test-SmartAppControlEnforced {
    try {
        $policy = Get-ItemProperty `
            "HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy" `
            -ErrorAction Stop
        return $policy.VerifiedAndReputablePolicyState -eq 1
    } catch {
        return $false
    }
}

function Find-CodeSigningCertificate {
    $now = Get-Date
    return Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert `
        -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Subject -eq $CodeSigningSubject -and
            $_.HasPrivateKey -and
            $_.NotBefore -le $now -and
            $_.NotAfter -gt $now
        } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1
}

function Find-SignTool {
    $fromPath = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $kitsBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/bin"
    if (Test-Path $kitsBin) {
        $candidate = Get-ChildItem $kitsBin -Recurse -Filter signtool.exe `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw "signtool.exe was not found. Install a Windows SDK with the Visual Studio C++ workload."
}

function Invoke-LocalCodeSigning {
    if ($NoCodeSign) {
        return
    }

    $certificate = Find-CodeSigningCertificate
    if (-not $certificate) {
        if (Test-SmartAppControlEnforced) {
            throw "Smart App Control is enforcing, but no usable '$CodeSigningSubject' certificate exists in Cert:\CurrentUser\My. Install the development signing identity or pass -NoCodeSign only on a machine where unsigned development binaries are allowed."
        }
        Write-Verbose "Skipping local code signing because '$CodeSigningSubject' was not found."
        return
    }

    $signTool = Find-SignTool
    $artifacts = Get-ChildItem $BuildDir -File |
        Where-Object { $_.Extension -in ".exe", ".dll" }
    foreach ($artifact in $artifacts) {
        $signature = Get-AuthenticodeSignature $artifact.FullName
        if ($signature.Status -eq "Valid") {
            continue
        }

        & $signTool sign `
            /fd SHA256 `
            /sha1 $certificate.Thumbprint `
            /s My `
            $artifact.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "Code signing failed for $($artifact.FullName) with exit code $LASTEXITCODE."
        }

        $signature = Get-AuthenticodeSignature $artifact.FullName
        if (-not $signature.SignerCertificate -or
            $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
            throw "The signature on $($artifact.FullName) was not created with the requested development certificate: $($signature.StatusMessage)"
        }
    }
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

Invoke-LocalCodeSigning
