<img alt="banner" src="https://github.com/user-attachments/assets/c3d1fa1e-30da-42f7-803f-1a4eb08cacb5" />
<div align="center">
  <a href="https://go.nsmbu.net/discord">
    <img alt="discord" height="56" src="https://github.com/user-attachments/assets/9c31e6cc-e2f8-4d53-9507-15a17b368088">
  </a>
  <a href="https://zenith.nsmbu.net/wiki/TuneBloom">
    <img alt="docs" height="56" src="https://github.com/user-attachments/assets/67b764c4-f2d0-470a-8f12-206d3d0203fc">
  </a>
</div>

## Overview
TuneBloom is a fast, polished, and stable editor for NintendoWare sound archives. It features low memory usage and a high degree of data accuracy.

You can safely edit everything in the archive, such as waves, sequences, streams, and banks. Live emulated audio playback allows previewing your work for a streamlined workflow.

**TuneBloom currently supports Wii U, 3DS, and Switch sound archives. (`BFSAR` and `BCSAR`)**

## Features
- Edit every property of a sound archive!
- Multi-track stream editing, including support for regions.
- Sophisticated sequence editor: Built-in manual, autocomplete, and hints.
- Detailed bank and instrument editor: ADSR envelope, live MIDI playback.
- Prepare wave files directly in the editor: normalization, trimming, and looping.
- Export contents to `WAV`, `MIDI`, `SF2`, and `DLS`.
- And even more!

## Screenshots
<div align="center">
<img width="400" alt="Stream Sounds List" src="https://github.com/user-attachments/assets/3f0f73ea-0e92-4363-b26f-161586f58919" />
<img width="400" alt="Bank & Instrument Editor" src="https://github.com/user-attachments/assets/7cf2642f-505b-4c25-b290-adbc4eed6abe" />
<img width="400" alt="Wave Sound Set List" src="https://github.com/user-attachments/assets/1242c32a-8661-44cc-91bf-6ba442d58a6e" />
<img width="400" alt="Sequence Editor" src="https://github.com/user-attachments/assets/ec5e20db-5ded-4e2b-99a5-6f47c84ba488" />
</div>

## Future Goals
- `BRSAR` support (Wii)

## Limitations
- Edited sound archives (`BFSAR` and `BCSAR`)
  - Sound archives saved by imperfect tools (such as Citric Composer) are likely to be corrupted in ways that prevent them from being loaded in TuneBloom.
- Audio Formats
  - Currently, only `WAV` files can be imported or exported. (No support for `MP3`, `FLAC`, `OGG`, or `AIFF`)
- ADTS Streams (`AAC`)
  - Only `BFSTM` and `BCSTM` stream files are supported.

> [!TIP]
> While TuneBloom has reached a stable release and creates automatic backups, it's still advised to make your own.

## Compiling
[Premake](https://premake.github.io/) is used as the build system which makes setting up a development environment very straightforward.

You must have Premake version **5.0.0-beta8** or higher.

> [!IMPORTANT]
> When cloning the repository, make sure to use `--recurse-submodules`!

### Windows
**Requirements**
- The [latest Premake pre-built binary](https://premake.github.io/download) for Windows (The version on WinGet is not up to date!)
- [Microsoft Visual Studio](https://visualstudio.microsoft.com/downloads/) 2022 or higher, with the [C++ Clang tools for Windows](https://learn.microsoft.com/en-us/cpp/build/clang-support-msbuild?view=msvc-170) component installed.
  - If you cannot use Clang on your system, you may instead use MSVC (see below).
- Windows 10 or 11

**Build**
- Run `premake5 vs2022` (Only needed to run on first setup, when a `premake5.lua` changes or when source files are added/deleted)
  - If using MSVC, add the flag `--cc=msc-v143`
- Open the generated `.sln` file in Visual Studio (2022 or higher) to edit & build

### macOS
**Requirements**
- Premake (`brew install premake`)
- Xcode Command Line Tools (`xcode-select --install`)
- macOS 14 or higher

**Build**
- Run `premake5 gmake` (Only needed to run on first setup, when a `premake5.lua` changes or when source files are added/deleted)
- Run `make config=release_glfw_arm64` (Release build) or `make config=debug_glfw_arm64` (Debug build)

### Linux
**Requirements**
- Install Premake from your system package manager if it is up to date, otherwise manually download the [latest Premake pre-built binary](https://premake.github.io/download) for Linux
- You may need to install X11/Wayland development headers packages for your distro

**Build**
- Run `premake5 gmake` (Only needed to run on first setup, when a `premake5.lua` changes or when source files are added/deleted)
- Run `make config=release_glfw_x86_64` (Release build) or `make config=debug_glfw_x86_64` (Debug build)

> [!TIP]
> On macOS and Linux, you can add `-j` to the `make` command to speed up compilation.

## Contributing
External contributions and pull requests are welcome! We understand that the quality of the code may not be fully up to par, so we appreciate your patience. Help and developer discussions are always available in our [Discord](https://go.nsmbu.net/discord) server.

## Credits
- [STUPID Modder](https://github.com/stupidestmodder) - Development lead
- [AboodXD](https://github.com/aboood40091) - Research & algorithms
- [Luminyx1](https://github.com/Luminyx1) - Build system & testing
- [jhmaster](https://github.com/jhmaster2000) - macOS assistance
- [Alex Barney](https://github.com/Thealexbarney) - DspTool (DspAdpcm Encoding)
- [Omar Cornut](https://github.com/ocornut) - ImGui
- [Vladimir Shatrov](https://github.com/frowrik) - ImGui piano keyboard
- [BalazsJako](https://github.com/BalazsJako) - ImGui text editor
- [samhocevar](https://github.com/samhocevar/portable-file-dialogs) - Portable File Dialogs
