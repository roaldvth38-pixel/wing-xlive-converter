param(
    [string]$PythonExe = "",
    [string]$InstallerBaseName = "",
    [switch]$SkipBuild,
    [switch]$SkipInstaller,
    [switch]$Help
)

if ($Help) {
    Write-Host "Wing X-LIVE Installer Packager"
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\package_wing_xlive_converter_installer.ps1"
    Write-Host "  .\package_wing_xlive_converter_installer.ps1 -SkipInstaller"
    Write-Host "  .\package_wing_xlive_converter_installer.ps1 -SkipBuild"
    Write-Host "  .\package_wing_xlive_converter_installer.ps1 -InstallerBaseName WingXLIVEConverterSetup_custom"
    Write-Host ""
    Write-Host "What it does:"
    Write-Host "  1) Builds dist\WingXLIVEConverter.exe (unless -SkipBuild)"
    Write-Host "  2) Creates a portable package in release\portable"
    Write-Host "  3) Builds an installer in release\installer if Inno Setup is available"
    exit 0
}

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $repoRoot

function Resolve-IsccPath {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    $fromPath = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    return $null
}

function Resolve-FfmpegPath {
    $candidates = @(
        (Join-Path $repoRoot "dist\ffmpeg.exe"),
        (Join-Path $repoRoot "ffmpeg.exe")
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    $fromPath = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    return $null
}

$buildScript = Join-Path $repoRoot "build_wing_xlive_converter_exe.ps1"
if (-not $SkipBuild) {
    Write-Host "Building WingXLIVEConverter.exe..."
    if ($PythonExe) {
        & $buildScript -PythonExe $PythonExe
    }
    else {
        & $buildScript
    }
}

$converterExe = Join-Path $repoRoot "dist\WingXLIVEConverter.exe"
if (-not (Test-Path $converterExe)) {
    throw "Missing converter executable: $converterExe"
}

$distFfmpeg = Join-Path $repoRoot "dist\ffmpeg.exe"
$ffmpegPath = Resolve-FfmpegPath
if ($ffmpegPath) {
    $resolvedSource = (Resolve-Path $ffmpegPath).Path
    $resolvedDist = $null
    if (Test-Path $distFfmpeg) {
        $resolvedDist = (Resolve-Path $distFfmpeg).Path
    }

    if (-not $resolvedDist -or ($resolvedSource -ine $resolvedDist)) {
        Copy-Item -Path $resolvedSource -Destination $distFfmpeg -Force
    }

    Write-Host "Using ffmpeg from: $resolvedSource"
    if (Test-Path $distFfmpeg) {
        Write-Host "ffmpeg.exe is already in dist."
    }
}
else {
    Write-Warning "ffmpeg.exe was not found. FLAC/MP3 will use in-process fallback on target machines."
}

$distFfmpegLicense = Join-Path $repoRoot "dist\FFMPEG_LICENSE.txt"
$ffmpegLicenseCandidates = @(
    (Join-Path $repoRoot "FFMPEG_LICENSE.txt"),
    $distFfmpegLicense
)
$ffmpegLicenseSource = $null
foreach ($candidate in $ffmpegLicenseCandidates) {
    if ($candidate -and (Test-Path $candidate)) {
        $ffmpegLicenseSource = (Resolve-Path $candidate).Path
        break
    }
}

if ($ffmpegLicenseSource -and ($ffmpegLicenseSource -ine $distFfmpegLicense)) {
    Copy-Item -Path $ffmpegLicenseSource -Destination $distFfmpegLicense -Force
}

$portableDir = Join-Path $repoRoot "release\portable"
if (Test-Path $portableDir) {
    Remove-Item -Path $portableDir -Recurse -Force
}
New-Item -ItemType Directory -Path $portableDir -Force | Out-Null

Copy-Item -Path $converterExe -Destination (Join-Path $portableDir "WingXLIVEConverter.exe") -Force

if (Test-Path $distFfmpeg) {
    Copy-Item -Path $distFfmpeg -Destination (Join-Path $portableDir "ffmpeg.exe") -Force
}

if (Test-Path $distFfmpegLicense) {
    Copy-Item -Path $distFfmpegLicense -Destination (Join-Path $portableDir "FFMPEG_LICENSE.txt") -Force
}

$converterReadme = Join-Path $repoRoot "README_XLIVE_CONVERTER.md"
if (Test-Path $converterReadme) {
    Copy-Item -Path $converterReadme -Destination (Join-Path $portableDir "README_XLIVE_CONVERTER.md") -Force
}

$portableReadme = @"
Wing X-LIVE Converter Portable Package

Contains:
- WingXLIVEConverter.exe

How to use:
1. Keep this file in the same folder as your recordings.
2. Run WingXLIVEConverter.exe.
3. WAV, FLAC, and MP3 export are available.

Notes:
- FLAC and MP3 prefer ffmpeg for fastest conversion when ffmpeg.exe is bundled.
- If ffmpeg is not available, the converter falls back to in-process encoding.
"@

Set-Content -Path (Join-Path $portableDir "PORTABLE_PACKAGE_README.txt") -Value $portableReadme -Encoding ascii
Write-Host "Portable package created at: $portableDir"

if ($SkipInstaller) {
    Write-Host "Installer build skipped by request (-SkipInstaller)."
    exit 0
}

$issPath = Join-Path $repoRoot "installer\WingXLIVEConverter.iss"
if (-not (Test-Path $issPath)) {
    throw "Missing Inno Setup script: $issPath"
}

$isccPath = Resolve-IsccPath
if (-not $isccPath) {
    Write-Warning "Inno Setup 6 (ISCC.exe) not found. Portable package is ready, but installer was not built."
    Write-Warning "Install Inno Setup 6, then rerun this script."
    exit 0
}

$installerOutDir = Join-Path $repoRoot "release\installer"
New-Item -ItemType Directory -Path $installerOutDir -Force | Out-Null

if (-not $InstallerBaseName) {
    $InstallerBaseName = "WingXLIVEConverterSetup_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
}

Write-Host "Building installer with ISCC: $isccPath"
Write-Host "Installer output base filename: $InstallerBaseName"
& $isccPath "/O$installerOutDir" "/F$InstallerBaseName" $issPath
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compilation failed."
}

$latestInstaller = Get-ChildItem -Path $installerOutDir -Filter "WingXLIVEConverterSetup*.exe" -File |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if ($latestInstaller) {
    Write-Host "Installer created: $($latestInstaller.FullName)"
}
else {
    Write-Warning "No installer EXE found in $installerOutDir after compilation."
}
