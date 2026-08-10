[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Action = "help",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Remaining
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot ".." )).Path
$IsWindowsHost = $env:OS -eq "Windows_NT"
$DefaultConfig = "Debug"

$Options = [ordered]@{
    Preset = if ($env:CGLAB_PRESET) { $env:CGLAB_PRESET } elseif ($IsWindowsHost) { "windows-msvc-ninja" } else { "linux-clang-ninja" }
    Config = if ($env:CGLAB_CONFIG) { $env:CGLAB_CONFIG } else { $DefaultConfig }
    Target = "TriangleSample"
    Jobs = ""
    Tests = "OFF"
    Legacy = "OFF"
    Fresh = $false
    Triplet = ""
    VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $Root "vcpkg" }
    VcpkgRootExplicit = [bool]$env:VCPKG_ROOT
    Frames = "6"
    Asset = ""
}

function Show-Help {
    @"
CGLab development entry point

Usage:
  .\scripts\cglab.ps1 <action> [options]

Actions:
  doctor       Check host tools and optional Vulkan runtime support.
  setup        Initialize submodules, pinned vcpkg, dependencies and Debug.
  deps         Bootstrap vcpkg and install the manifest for the selected triplet.
  configure    Configure one CMake preset.
  build        Build one target (default: TriangleSample).
  test         Configure with tests, build all and run CTest.
  smoke        Build and run TriangleSample or GltfSponzaSample.
  clean        Run the CMake clean target for the selected preset.

Options:
  --preset <name>       Toolchain preset (windows-msvc-ninja or linux-clang-ninja),
                        or a complete/custom configure preset name.
  --config <type>       Debug, Release or RelWithDebInfo.
  --target <name>       CMake target for build, or app target for smoke.
  --jobs <count>        Parallel build jobs.
  --fresh               Use cmake --fresh when configuring.
  --tests <ON|OFF>      Configure CTest targets.
  --legacy <ON|OFF>     Configure VulkanSampleLegacy.
  --triplet <name>      vcpkg target triplet.
  --vcpkg-root <path>   vcpkg checkout location.
  --frames <count>      GPU smoke frame count.
  --asset <path>        glTF/GLB asset for GltfSponzaSample.
"@ | Write-Host
}

function Fail([string]$Message) {
    throw $Message
}

function Parse-Arguments([string[]]$Arguments) {
    for ($i = 0; $i -lt $Arguments.Count; $i++) {
        $token = $Arguments[$i]
        if ($token -eq "--fresh") {
            $Options.Fresh = $true
            continue
        }
        if (-not $token.StartsWith("--")) {
            Fail "Unknown argument '$token'. Use --help for usage."
        }

        $key = $token.Substring(2)
        if ($key.Contains("=")) {
            $parts = $key.Split("=", 2)
            $key = $parts[0]
            $value = $parts[1]
        } else {
            if ($i + 1 -ge $Arguments.Count) {
                Fail "Missing value for --$key."
            }
            $i++
            $value = $Arguments[$i]
        }

        switch ($key) {
            "preset" { $Options.Preset = $value }
            "config" { $Options.Config = $value }
            "target" { $Options.Target = $value }
            "jobs" { $Options.Jobs = $value }
            "tests" { $Options.Tests = $value.ToUpperInvariant() }
            "legacy" { $Options.Legacy = $value.ToUpperInvariant() }
            "triplet" { $Options.Triplet = $value }
            "vcpkg-root" { $Options.VcpkgRoot = $value; $Options.VcpkgRootExplicit = $true }
            "frames" { $Options.Frames = $value }
            "asset" { $Options.Asset = $value }
            "help" { Show-Help; exit 0 }
            default { Fail "Unknown option --$key. Use --help for usage." }
        }
    }
}

function Invoke-Native([string]$Command, [string[]]$Arguments = @()) {
    & $Command @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Fail "Command failed with exit code ${LASTEXITCODE}: $Command $($Arguments -join ' ')"
    }
}

function Get-CommandPath([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

function Add-DiscoveredToolPaths {
    if (-not $IsWindowsHost) { return }
    $directories = @(
        (Join-Path ${env:ProgramFiles} "Ninja"),
        (Join-Path ${env:ProgramFiles} "CMake\bin")
    )
    if ($env:VULKAN_SDK) { $directories += (Join-Path $env:VULKAN_SDK "Bin") }
    $current = @($env:Path -split ";")
    foreach ($directory in $directories) {
        if ((Test-Path $directory) -and ($current -notcontains $directory)) {
            $env:Path = "$directory;$env:Path"
            $current += $directory
        }
    }
}

function Find-VsDevCmd {
    if (-not $IsWindowsHost) { return $null }
    $vswhere = Get-CommandPath "vswhere.exe"
    if (-not $vswhere) {
        $installerPath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $installerPath) { $vswhere = $installerPath }
    }
    if (-not $vswhere) { return $null }

    $installPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if (-not $installPath) { return $null }
    $vsdev = Join-Path $installPath.Trim() "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $vsdev) { return $vsdev }
    return $null
}

function Find-LatestMsvcVersion {
    $vswhere = Get-CommandPath "vswhere.exe"
    if (-not $vswhere) {
        $installerPath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $installerPath) { $vswhere = $installerPath }
    }
    if (-not $vswhere) { return $null }
    $compilerPaths = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe")
    $versions = foreach ($compilerPath in $compilerPaths) {
        if ($compilerPath -match "\\MSVC\\(\d+\.\d+)(?:\.\d+)?\\bin\\Hostx64\\x64\\cl\.exe$") {
            $Matches[1]
        }
    }
    return ($versions | Sort-Object { [version]$_ } | Select-Object -Last 1)
}

function Import-MsvcEnvironment {
    if (-not $IsWindowsHost) { return }
    if (Get-CommandPath "cl.exe") { return }

    $vsdev = Find-VsDevCmd
    if (-not $vsdev) {
        Fail "MSVC was not found. Install Visual Studio 2022 with the Desktop C++ workload, or start this command from a VS Developer Command Prompt."
    }

    $toolset = Find-LatestMsvcVersion
    $toolsetArgument = if ($toolset) { " -vcvars_ver=$toolset" } else { "" }
    $command = 'call "' + $vsdev + '" -arch=x64 -host_arch=x64' + $toolsetArgument + ' >nul && set'
    $lines = & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { Fail "Unable to load the Visual Studio C++ environment from $vsdev" }
    foreach ($line in $lines) {
        $separator = $line.IndexOf("=")
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
        }
    }
    if (-not (Get-CommandPath "cl.exe")) { Fail "Visual Studio environment was loaded but cl.exe is still unavailable." }
}

function Get-ResolvedPreset {
    $name = $Options.Preset
    if ($name -match "-(debug|release|relwithdebinfo)$") { return $name }
    if ($name -in @("windows-msvc-ninja", "linux-clang-ninja")) {
        return "$name-$($Options.Config.ToLowerInvariant())"
    }
    return $name
}

function Get-DefaultTriplet([string]$Preset) {
    if ($Options.Triplet) { return $Options.Triplet }
    if ($Preset.StartsWith("linux-")) { return "x64-linux" }
    return "x64-windows"
}

function Set-VcpkgEnvironment {
    [Environment]::SetEnvironmentVariable("VCPKG_ROOT", [IO.Path]::GetFullPath($Options.VcpkgRoot), "Process")
}

function Get-BuildDirectory([string]$Preset) {
    if ($env:CGLAB_BUILD_DIR) { return $env:CGLAB_BUILD_DIR }
    $known = @{
        "windows-msvc-ninja-debug" = "build\windows-msvc-ninja\Debug"
        "windows-msvc-ninja-release" = "build\windows-msvc-ninja\Release"
        "windows-msvc-ninja-relwithdebinfo" = "build\windows-msvc-ninja\RelWithDebInfo"
        "linux-clang-ninja-debug" = "build/linux-clang-ninja/Debug"
        "linux-clang-ninja-release" = "build/linux-clang-ninja/Release"
        "linux-clang-ninja-relwithdebinfo" = "build/linux-clang-ninja/RelWithDebInfo"
    }
    if ($known.ContainsKey($Preset)) { return Join-Path $Root $known[$Preset] }
    return $null
}

function Test-Doctor {
    $failures = [System.Collections.Generic.List[string]]::new()
    Write-Host "CGLab environment doctor" -ForegroundColor Cyan

    foreach ($tool in @("git", "cmake", "ninja")) {
        if (Get-CommandPath "$tool.exe") { Write-Host "  [OK] $tool" } elseif (Get-CommandPath $tool) { Write-Host "  [OK] $tool" } else { $failures.Add($tool); Write-Host "  [MISSING] $tool" -ForegroundColor Red }
    }

    if ($IsWindowsHost) {
        if (Find-VsDevCmd) { Write-Host "  [OK] Visual Studio C++ workload" } else { $failures.Add("Visual Studio C++ workload"); Write-Host "  [MISSING] Visual Studio C++ workload" -ForegroundColor Red }
    } else {
        foreach ($tool in @("clang", "clang++")) {
            if (Get-CommandPath $tool) { Write-Host "  [OK] $tool" } else { $failures.Add($tool); Write-Host "  [MISSING] $tool" -ForegroundColor Red }
        }
    }

    if (Test-Path (Join-Path $Options.VcpkgRoot ".git")) { Write-Host "  [OK] vcpkg checkout: $($Options.VcpkgRoot)" } else { Write-Host "  [INFO] vcpkg will be bootstrapped by setup" -ForegroundColor Yellow }
    if (Test-Path (Join-Path $Root "third_party\render-graph\.git")) { Write-Host "  [OK] submodules" } else { Write-Host "  [INFO] submodules will be initialized by setup" -ForegroundColor Yellow }

    if ((Get-CommandPath "glslc.exe") -or (Get-CommandPath "glslc")) { Write-Host "  [OK] glslc (shader rebuild)" } else { Write-Host "  [OPTIONAL] glslc not found; checked-in SPIR-V can still be used" -ForegroundColor Yellow }
    if ($env:VULKAN_SDK) { Write-Host "  [OK] VULKAN_SDK=${env:VULKAN_SDK}" } else { Write-Host "  [OPTIONAL] VULKAN_SDK is not set" -ForegroundColor Yellow }
    if ((Get-CommandPath "vulkaninfo.exe") -or (Get-CommandPath "vulkaninfo")) { Write-Host "  [OK] Vulkan runtime query" } else { Write-Host "  [OPTIONAL] vulkaninfo not found; GPU smoke may require a driver/runtime" -ForegroundColor Yellow }

    if ($failures.Count -gt 0) {
        Write-Host "`nInstall or enable the missing required tools, then rerun doctor." -ForegroundColor Red
        if ($IsWindowsHost) { Write-Host "Suggested Windows tools: Visual Studio 2022 Desktop C++, CMake and Ninja." }
        else { Write-Host "Suggested Linux packages: git, cmake, ninja-build, clang and clang++." }
        return $false
    }
    return $true
}

function Ensure-Submodules {
    Push-Location $Root
    try {
        Invoke-Native "git" @("submodule", "sync", "--recursive")
        Invoke-Native "git" @("submodule", "update", "--init", "--recursive")
    } finally { Pop-Location }
}

function Get-VcpkgBaseline {
    $manifest = Get-Content (Join-Path $Root "vcpkg.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $manifest.'builtin-baseline') { Fail "vcpkg.json does not contain builtin-baseline." }
    return $manifest.'builtin-baseline'
}

function Ensure-Vcpkg {
    $path = [IO.Path]::GetFullPath($Options.VcpkgRoot)
    $baseline = Get-VcpkgBaseline
    $expectedRemote = "https://github.com/microsoft/vcpkg.git"
    if (Test-Path $path) {
        if (-not (Test-Path (Join-Path $path ".git"))) { Fail "Vcpkg path exists but is not a git checkout: $path" }
        $remote = (& git -C $path config --get remote.origin.url).Trim().TrimEnd("/")
        if ($remote -eq "git@github.com:microsoft/vcpkg.git") {
            Invoke-Native "git" @("-C", $path, "remote", "set-url", "origin", $expectedRemote)
            $remote = $expectedRemote
        }
        if ($remote -ne $expectedRemote -and $remote -ne "$expectedRemote.git") { Fail "Existing vcpkg remote is '$remote', expected '$expectedRemote'. No files were changed." }
        $status = (& git -C $path status --porcelain)
        if ($status) { Fail "Existing vcpkg checkout has local changes. Commit or move them before setup; no files were changed." }
    } else {
        New-Item -ItemType Directory -Force -Path (Split-Path $path -Parent) | Out-Null
        Invoke-Native "git" @("clone", $expectedRemote, $path)
    }

    $baselineRef = "${baseline}^{commit}"
    & git -C $path cat-file -e $baselineRef 2>$null
    $baselinePresent = $LASTEXITCODE -eq 0
    if (-not $baselinePresent) { Invoke-Native "git" @("-C", $path, "fetch", "origin", $baseline) }
    Invoke-Native "git" @("-C", $path, "checkout", "--detach", $baseline)
    [Environment]::SetEnvironmentVariable("VCPKG_ROOT", $path, "Process")
    $bootstrap = if ($IsWindowsHost) { Join-Path $path "bootstrap-vcpkg.bat" } else { Join-Path $path "bootstrap-vcpkg.sh" }
    $executable = if ($IsWindowsHost) { Join-Path $path "vcpkg.exe" } else { Join-Path $path "vcpkg" }
    if (-not (Test-Path $bootstrap)) { Fail "vcpkg bootstrap script is missing at $bootstrap" }
    $sentinel = Join-Path $Root ".cglab-vcpkg-bootstrap-baseline"
    $sentinelValue = if (Test-Path $sentinel) { (Get-Content $sentinel -Raw).Trim() } else { "" }
    if (-not (Test-Path $executable) -or $sentinelValue -ne $baseline) {
        Push-Location $path
        try { Invoke-Native $bootstrap @("-disableMetrics") } finally { Pop-Location }
        Set-Content -LiteralPath $sentinel -Value $baseline -NoNewline -Encoding ASCII
    }
    return $executable
}

function Install-Dependencies {
    $preset = Get-ResolvedPreset
    if ($preset.StartsWith("windows-")) { Import-MsvcEnvironment }
    $vcpkg = Ensure-Vcpkg
    Set-VcpkgEnvironment
    $triplet = Get-DefaultTriplet $preset
    Push-Location $Root
    try { Invoke-Native $vcpkg @("install", "--triplet", $triplet) } finally { Pop-Location }
}

function Configure-Project([bool]$ForceTests = $false) {
    $preset = Get-ResolvedPreset
    if ($preset.StartsWith("windows-")) { Import-MsvcEnvironment }
    Set-VcpkgEnvironment
    if (-not (Test-Path (Join-Path $Options.VcpkgRoot "scripts\buildsystems\vcpkg.cmake"))) {
        Fail "vcpkg is not initialized. Run scripts/cglab.ps1 setup first."
    }
    $arguments = @()
    if ($Options.Fresh) { $arguments += "--fresh" }
    if ($Options.VcpkgRootExplicit) {
        $arguments += "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $Options.VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')"
    }
    $arguments += @("--preset", $preset, "-DBUILD_TESTING=$(if ($ForceTests) { 'ON' } else { $Options.Tests })", "-DCGLAB_BUILD_LEGACY_VULKAN_SAMPLE=$($Options.Legacy)")
    Push-Location $Root
    try { Invoke-Native "cmake" $arguments } finally { Pop-Location }
    return $preset
}

function Build-Target([string]$Preset, [string]$Target) {
    if ($Preset.StartsWith("windows-")) { Import-MsvcEnvironment }
    Set-VcpkgEnvironment
    $arguments = @("--build", "--preset", $Preset, "--target", $Target)
    if ($Options.Jobs) { $arguments += @("--parallel", $Options.Jobs) }
    Push-Location $Root
    try { Invoke-Native "cmake" $arguments } finally { Pop-Location }
}

function Find-BuiltExecutable([string]$Preset, [string]$Target) {
    $buildDirectory = Get-BuildDirectory $Preset
    $name = if ($IsWindowsHost) { "$Target.exe" } else { $Target }
    if ($buildDirectory) {
        $candidate = Join-Path $buildDirectory $name
        if (Test-Path $candidate) { return $candidate }
    }
    $candidate = Get-ChildItem (Join-Path $Root "build") -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($candidate) { return $candidate.FullName }
    Fail "Built executable '$name' was not found under build/."
}

function Run-Smoke {
    $preset = Get-ResolvedPreset
    $target = $Options.Target
    if ($target -ne "TriangleSample" -and $target -ne "GltfSponzaSample") { Fail "Smoke target must be TriangleSample or GltfSponzaSample." }
    Build-Target $preset $target
    $executable = Find-BuiltExecutable $preset $target
    $arguments = @("--smoke-test", "--frames", $Options.Frames, "--validation", "--no-ui")
    if ($target -eq "GltfSponzaSample") {
        $asset = if ($Options.Asset) { $Options.Asset } else { "assets/triangle.gltf" }
        $arguments += @("--asset", $asset)
    }
    Push-Location $Root
    try { Invoke-Native $executable $arguments } finally { Pop-Location }
}

try {
    if ($Action -in @("help", "-h", "--help")) { Show-Help; exit 0 }
    Parse-Arguments $Remaining
    Add-DiscoveredToolPaths
    switch ($Action.ToLowerInvariant()) {
        "doctor" {
            if (-not (Test-Doctor)) { exit 1 }
        }
        "setup" {
            if (-not (Test-Doctor)) { exit 1 }
            Ensure-Submodules
            Install-Dependencies
            $Options.Fresh = $true
            Configure-Project
        }
        "deps" { Install-Dependencies }
        "configure" { Configure-Project | Out-Null }
        "build" { Build-Target (Configure-Project) $Options.Target }
        "test" {
            $preset = Configure-Project $true
            Build-Target $preset "all"
            Push-Location $Root
            try { Invoke-Native "ctest" @("--preset", $preset) } finally { Pop-Location }
        }
        "smoke" { Run-Smoke }
        "clean" {
            $preset = Get-ResolvedPreset
            Build-Target $preset "clean"
        }
        default { Fail "Unknown action '$Action'. Use --help for usage." }
    }
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
