# Mix Balancer + DrumBusUltimate VST3 Plugins (JUCE + CMake)

JUCE-based VST3 plugin project using CMake, now containing three effects:

- `Mix Balancer` (standalone real-time gain-staging and balance analyzer)
- `DrumBusUltimate` (drum bus compressor -> limiter -> clipper)
- `Synchronized Multitrack Tape` (multitrack tape saturation with global wow/flutter)

## Behringer WING X-LIVE Converter App

This repository now also includes a standalone app for converting Behringer WING
X-LIVE recording folders into DAW-ready multitrack WAV files.

- Script: `wing_xlive_multitrack_converter.py`
- Guide: `README_XLIVE_CONVERTER.md`

Quick example:

```powershell
python wing_xlive_multitrack_converter.py "D:/X-LIVE/SESSION_01" "D:/Exports/SESSION_01"
```

## Prerequisites

- CMake 3.22+
- Visual Studio 2022 with Desktop development with C++
- Git (needed when JUCE is fetched automatically)

## Configure

```powershell
cmake --preset vs2022-release
```

## Build

```powershell
cmake --build --preset vs2022-release
```

## Debug Build

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
```

## Output

The compiled VST3 plugin is generated in the build output folders created by CMake and Visual Studio.

For DrumBusUltimate, the installed VST3 path is:

`C:\Program Files\Common Files\VST3\DrumBusUltimate.vst3`

## Bass Channel Strip Installers (Windows + macOS)

### Windows installer (.exe)

Requirements:

- Inno Setup 6 (for `ISCC.exe`)
- CMake build already configured for this repo

Generate the installer:

```powershell
.\package_basschannelstrip_vst3_windows_installer.ps1
```

Or with the batch wrapper:

```bat
package_basschannelstrip_vst3_windows_installer.bat
```

Output location:

- `release\installer\BassChannelStrip_VST3_Setup_<timestamp>.exe`

### macOS installer (.pkg)

Requirements:

- macOS machine
- Xcode command line tools (`pkgbuild`, `productbuild`)
- A successful macOS build of `BassChannelStrip_VST3`

Generate the installer on macOS:

```bash
./package_basschannelstrip_vst3_mac_installer.sh --build-dir ./build-mac --config Release
```

Or with a CMake build preset:

```bash
./package_basschannelstrip_vst3_mac_installer.sh --build-preset mac-release --config Release
```

Output location:

- `release/installer-macos/BassChannelstrip_VST3_<version>_<timestamp>.pkg`

### Build macOS installer via GitHub Actions

Workflow file:

- `.github/workflows/build-basschannelstrip-macos-installer.yml`

How to run:

1. Push your branch to GitHub.
2. Open the repository Actions tab.
3. Run workflow: `Build BassChannelStrip macOS Installer`.
4. Optionally enable `sign_pkg` if signing secrets are configured.
5. Download the produced macOS artifact from the workflow run.

Target selection behavior:

1. If BassChannelStrip sources/target exist, it builds and packages `BassChannelStrip_VST3`.
2. Otherwise it falls back to `Juice_VST3`.
3. Output naming is always Bass Channelstrip (`BassChannelstrip_VST3...pkg` and `BassChannelstrip-macos-pkg`).

Automatic trigger:

1. Push a git tag that starts with `v` (example: `v1.2.0`).
2. The workflow runs automatically and uses `1.2.0` as package version.
3. Tag-triggered runs default to unsigned packaging.
4. Publishing a GitHub Release also triggers the workflow and uses the release tag version.

## DrumBusUltimate Signal Flow

The new plugin processes in this order:

1. Input trim and sidechain HPF detector.
2. Soft-knee compressor with attack/release, ratio, threshold, and makeup.
3. Brickwall-style peak limiter stage with adjustable release and ceiling.
4. Variable soft/hard clipper with drive and blend.
5. Wet/dry mix and output trim.

Main controls include:

- Global: Input, Output, Mix
- Compressor: Threshold, Ratio, Attack, Release, Knee, Makeup, SC HPF
- Limiter: Ceiling, Release
- Clipper: Drive, Shape (soft to hard), Mix

## Synchronized Multitrack Tape

This plugin is designed for multitrack use where tape movement must stay coherent
across all tracks.

- Link group and master assignment are not used.
- Opening any plugin instance also opens a separate global control window.
- The global window provides shared `Wow` and `Flutter` controls for all active instances.
- Phase control is intentionally removed.

## Notes

- Default setting fetches JUCE automatically during CMake configure.
- To use a local JUCE checkout or package, set `JUCE_FETCH=OFF` and provide JUCE through `find_package`.

## Mix Balancer Workflow

The Mix Balancer plugin is a standalone analyzer with unity-gain pass-through.

1. Insert one instance on each track you want analyzed.
2. The plugin auto-registers each instance (no manual slot limit or assignment).
3. Read live LUFS/RMS/peak and low-mid-high energy split.
4. Use `Set Suggested` to auto-apply Level, Pan, EQ Cut, and EQ Freq for this instance.
5. Use `Set Suggested To All` to apply those settings to all currently active instances.
6. Check the Hold max line to see maximum reached values for Level, Pan, and EQ Cut.

## Troubleshooting

### "Workspace is not configured" in Debug/Run

1. Run the task "Configure CMake (VS2022)" first.
2. Run the task "Build CMake Release".
3. Start the launch profile "Launch Mix Balancer Standalone (Release)".

This project is configured to expose a JUCE Standalone app target for debug launch and still builds a VST3 plugin.

The workspace uses CMake presets, so no manual CMake kit selection is required.

### Recommended VS Code debug flow

1. Run the task "Configure CMake (Debug)".
2. Run the task "Build CMake Debug".
3. Start the launch profile "Launch Mix Balancer Standalone (Debug)".

### No Visual Studio instance found

Install one of the following, then restart VS Code:

- Visual Studio 2022 with "Desktop development with C++"
- Build Tools for Visual Studio 2022 with MSVC + Windows SDK + CMake tools

After install, rerun "Configure CMake (VS2022)".

## AI Company (ChatDev-style for VST)

This repository now also contains an AI-agent company workflow for VST plugin development.

- Folder: `ai_company/`
- Entry point: `ai_company/agent_company.py`
- Advanced entry point: `ai_company/agent_company_v2.py`
- Docs: `ai_company/README.md`

Run it from the project root:

```powershell
python ai_company/agent_company.py
```

Or run the upgraded flow with tester-driven CMake validation:

```powershell
python ai_company/agent_company_v2.py --cmake-check
```

Each run creates a task output folder under `ai_company/out/` containing:

- inter-agent conversation log
- generated JUCE plugin stub code
- review report
- test report
- improvement notes for next iterations
