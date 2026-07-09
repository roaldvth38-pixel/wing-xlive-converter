param(
    [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $repoRoot

function Resolve-PythonExe {
    param([string]$Candidate)

    if ($Candidate -and (Test-Path $Candidate)) {
        return (Resolve-Path $Candidate).Path
    }

    $known = @(
        (Join-Path $repoRoot ".venv-1\Scripts\python.exe"),
        (Join-Path $repoRoot ".venv\Scripts\python.exe")
    )

    foreach ($path in $known) {
        if (Test-Path $path) {
            return (Resolve-Path $path).Path
        }
    }

    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCommand) {
        return $pythonCommand.Source
    }

    throw "No Python executable found. Pass -PythonExe or create a virtual environment."
}

$python = Resolve-PythonExe -Candidate $PythonExe
Write-Host "Using Python: $python"

# Do not let a missing package probe abort the script when native-command
# nonzero exit codes are treated as errors (PowerShell 7 behavior).
$nativeErrPrefVar = Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue
if ($nativeErrPrefVar) {
    $oldNativeErrPref = $PSNativeCommandUseErrorActionPreference
    $PSNativeCommandUseErrorActionPreference = $false
}

& $python -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('PyInstaller') else 1)"
$pyInstallerInstalled = ($LASTEXITCODE -eq 0)

if ($nativeErrPrefVar) {
    $PSNativeCommandUseErrorActionPreference = $oldNativeErrPref
}

if (-not $pyInstallerInstalled) {
    Write-Host "Installing PyInstaller..."
    & $python -m pip install pyinstaller
}

$requiredPackages = @("numpy", "soundfile", "lameenc", "pillow")
foreach ($pkg in $requiredPackages) {
    if ($nativeErrPrefVar) {
        $oldNativeErrPref = $PSNativeCommandUseErrorActionPreference
        $PSNativeCommandUseErrorActionPreference = $false
    }

    & $python -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('$pkg') else 1)"
    $installed = ($LASTEXITCODE -eq 0)

    if ($nativeErrPrefVar) {
        $PSNativeCommandUseErrorActionPreference = $oldNativeErrPref
    }

    if (-not $installed) {
        Write-Host "Installing Python package: $pkg"
        & $python -m pip install $pkg
    }
}

Write-Host "Building WingXLIVEConverter.exe..."
& $python -m PyInstaller --noconfirm --clean --onefile --windowed --name WingXLIVEConverter --add-data "assets;assets" wing_xlive_converter_gui.py

$exePath = Join-Path $repoRoot "dist\WingXLIVEConverter.exe"
if (-not (Test-Path $exePath)) {
    throw "Build finished without expected output at $exePath"
}

Write-Host "Build complete: $exePath"
