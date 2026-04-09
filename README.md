# Juice VST3 Plugin (JUCE + CMake)

A minimal JUCE-based VST3 audio effect plugin project using CMake.

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

## Notes

- Default setting fetches JUCE automatically during CMake configure.
- To use a local JUCE checkout or package, set `JUCE_FETCH=OFF` and provide JUCE through `find_package`.

## Production Workflow

The plugin now ships with two built-in host programs:

- `15 ips Color`
- `30 ips Clean`

### Validation Checklist

Use this quick pass before printing/finalizing:

1. Gain-match bypass vs engaged within about 0.5 dB.
2. Check both `15 ips` and `30 ips` on full-range material.
3. Sweep `Azimuth` from subtle to extreme and verify no unstable image collapse.
4. Validate preset save/load roundtrip with the `Presets` menu in the plugin UI.
5. Confirm host program switching shows only `15 ips Color` and `30 ips Clean`.

## Troubleshooting

### "Workspace is not configured" in Debug/Run

1. Run the task "Configure CMake (VS2022)" first.
2. Run the task "Build CMake Release".
3. Start the launch profile "Launch Juice Standalone (Release)".

This project is configured to expose a JUCE Standalone app target for debug launch and still builds a VST3 plugin.

The workspace uses CMake presets, so no manual CMake kit selection is required.

### Recommended VS Code debug flow

1. Run the task "Configure CMake (Debug)".
2. Run the task "Build CMake Debug".
3. Start the launch profile "Launch Juice Standalone (Debug)".

### No Visual Studio instance found

Install one of the following, then restart VS Code:

- Visual Studio 2022 with "Desktop development with C++"
- Build Tools for Visual Studio 2022 with MSVC + Windows SDK + CMake tools

After install, rerun "Configure CMake (VS2022)".
