# NIME 2026 JUCE Project

This project is built using JUCE and CMake, utilizing the Visual Studio 2022 MSVC compiler.

## Prerequisites
- **CMake** (v3.15 or higher)
- **Visual Studio 2022** (with "Desktop development with C++" workload installed)
- **Visual Studio Code** (Recommended)

## How to Build via Command Line (PowerShell)

CMake acts as a meta-build system. First, you configure the project (which generates the Visual Studio files in the `build/` folder), and then you instruct CMake to execute the build.

### 1. Configure the Project
Run the following command in the root of the project to generate the build files:
```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
```
*Note: CMake automatically detects Visual Studio 2022 on your system.*

### 2. Build the Plugin
Once configured, compile the plugin using:
```powershell
cmake --build build --config Release
```
Your compiled `.vst3` or standalone application will be located inside the `build/NIMEReceiver_artefacts/` directory.

## How to Build in Visual Studio Code

1. Press `Ctrl + Shift + B`.
2. Select **CMake: Build Release** from the dropdown menu.
*(If you modify the CMakeLists.txt or add new files, run the **CMake: Configure** task first).*

## Troubleshooting

- **Missing `JuceHeader.h`**: Ensure `juce_generate_juce_header(NIMEReceiver)` exists in `CMakeLists.txt`.
- **Spaces in Bundle ID**: Ensure the `BUNDLE_ID` in `juce_add_plugin` uses dots without spaces (e.g., `com.NIMEProject.NIMEReceiver`).
- **Clean Rebuild**: If things get stuck, delete the `build/` folder entirely and run the Configure step again.