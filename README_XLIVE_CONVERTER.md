# Behringer WING X-LIVE to Multitrack Converter

This app converts Behringer WING X-LIVE recording folders into consolidated multitrack files.

- Input: X-LIVE take folders containing per-channel WAV files, including split segments.
- Output: one continuous file per track (WAV, FLAC, or MP3).

You can use it either as:

- CLI tool (script mode)
- Desktop app (GUI)
- Windows EXE (built from the GUI)

## What it handles

- Recursively scans folders for WAV files.
- Detects track numbers from common X-LIVE naming styles:
  - CH01_0001.wav
  - Track01-0002.wav
  - 01_0003.wav
- Detects takes automatically, including common card/track subfolders.
- Stitches segments in order into one WAV per channel.
- Verifies WAV format consistency while stitching each track.

## Quick start (Windows PowerShell)

```powershell
C:/Users/Gebruiker/AppData/Local/Programs/Python/Python312/python.exe wing_xlive_multitrack_converter.py "D:/X-LIVE/SESSION_01" "D:/Exports/SESSION_01"
```

## Desktop app (GUI)

Run the GUI directly with Python:

```powershell
python wing_xlive_converter_gui.py
```

The GUI includes:

- input/output folder pickers
- output type selection (WAV, FLAC, MP3)
- automatic conversion settings (no manual tuning required)
- MP3 output controls similar to renderer workflows (CBR/VBR mode, bitrate, quality)
- live conversion log view

## Usage

```text
wing_xlive_multitrack_converter.py INPUT_FOLDER OUTPUT_FOLDER [--dry-run] [--strict] [--no-recursive] [--verbose]
```

Arguments:

- INPUT_FOLDER: X-LIVE recording folder or parent folder that contains take folders.
- OUTPUT_FOLDER: destination folder for exported multitrack WAV files.

Options:

- --dry-run: detect takes/tracks and show the conversion plan only.
- --strict: fail if any WAV file cannot be mapped to a track number.
- --no-recursive: only scan the top-level input folder.
- --verbose: print additional logging.

## Build a Windows EXE

Build script (PowerShell):

```powershell
.\build_wing_xlive_converter_exe.ps1
```

Or double-click the batch wrapper:

```text
build_wing_xlive_converter_exe.bat
```

Output EXE:

```text
dist\WingXLIVEConverter.exe
```

After build, run the EXE directly (no terminal needed).

## Build Installer (EXE)

Use the packaging script to build an installer:

```powershell
.\package_wing_xlive_converter_installer.ps1
```

Or use the batch wrapper:

```text
build_wing_xlive_converter_installer.bat
```

What this packaging step does:

- builds `dist\WingXLIVEConverter.exe` (unless you pass `-SkipBuild`)
- creates a portable package at `release\portable`
- builds an installer at `release\installer` if Inno Setup 6 is installed

If Inno Setup is not installed, the script still creates the portable package and prints a warning.

## Build Official Installers (Windows + macOS)

### Windows (official EXE installer)

Use the existing packaging script:

```powershell
.\package_wing_xlive_converter_installer.ps1
```

Output:

```text
release\installer\WingXLIVEConverterSetup_*.exe
```

### macOS (official PKG installer)

Run these scripts on a macOS machine:

```bash
chmod +x ./build_wing_xlive_converter_mac_app.sh ./package_wing_xlive_converter_mac_installer.sh
./package_wing_xlive_converter_mac_installer.sh
```

Unsigned output:

```text
release/installer-macos/WingXLIVEConverter-macOS-1.0.0.pkg
```

For an official distributable mac installer (Gatekeeper-friendly), sign and notarize:

```bash
./package_wing_xlive_converter_mac_installer.sh \
  --sign-app-identity "Developer ID Application: Your Company (TEAMID)" \
  --sign-installer-identity "Developer ID Installer: Your Company (TEAMID)" \
  --notarize-profile AC_NOTARY
```

Requirements for official mac distribution:

- Apple Developer membership
- Developer ID Application certificate
- Developer ID Installer certificate
- Xcode command line tools (`codesign`, `productbuild`, `notarytool`)
- A configured notarytool keychain profile

### Build macOS installer from Windows (remote CI)

If you are on Windows, you cannot create a real `.pkg` locally because Apple packaging tools only exist on macOS.

This repository includes a GitHub Actions workflow that builds the mac installer on a macOS runner:

```text
.github/workflows/build-macos-installer.yml
```

How to use it:

1. Push your branch to GitHub.
2. Open Actions -> `Build macOS Installer` -> Run workflow.
3. Enter `app_version` (optional) and choose whether to sign/notarize.
4. Download the `WingXLIVEConverter-macOS-pkg` artifact from the run.

For signed/notarized output, configure repository secrets:

- `MAC_SIGN_APP_IDENTITY`
- `MAC_SIGN_INSTALLER_IDENTITY`
- `MAC_NOTARIZE_PROFILE`

### Build Windows and macOS installers in one run

This repository also includes a combined workflow:

```text
.github/workflows/build-release-installers.yml
```

Use it when you want one GitHub Actions run to build both installers.

How to use:

1. Push your branch to GitHub.
2. Open Actions -> `Build Release Installers` -> Run workflow.
3. Set inputs:
  - `app_version` (for naming)
  - `sign_and_notarize_macos` (optional)
  - `create_github_release` (optional)
4. Download artifacts:
  - `WingXLIVEConverter-windows-installer`
  - `WingXLIVEConverter-windows-portable`
  - `WingXLIVEConverter-macos-pkg`

If `create_github_release` is enabled, the workflow publishes a GitHub Release and attaches the Windows `.exe` and macOS `.pkg` files automatically.

## Typical workflow

1. Copy your X-LIVE recording folder(s) from SD media to a local drive.
2. Run the converter command.
3. Import exported TrackXX files into your DAW as aligned multitracks.

## Notes

- The converter now writes tracks directly to the selected output format (WAV, FLAC, or MP3).
- FLAC and MP3 are encoded during conversion (no intermediate Track*.wav step).
- FLAC/MP3 output prefers ffmpeg when available and falls back to in-process encoding when ffmpeg is not found.
- The MP3 "Write ReplayGain tag" option is currently reserved and not yet applied by the encoder.
- Files that do not match common track naming patterns are skipped unless --strict is used.
