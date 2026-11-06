[![GitHub release (latest by date)](https://img.shields.io/github/v/release/Edgetx/edgetx)](https://github.com/EdgeTX/edgetx/releases/latest)
[![GitHub all releases](https://img.shields.io/github/downloads/EdgeTX/edgetx/total)](https://github.com/EdgeTX/edgetx/releases)
[![GitHub license](https://img.shields.io/github/license/Edgetx/edgetx)](https://github.com/EdgeTX/edgetx/blob/main/LICENSE)
[![Commit Tests](https://github.com/EdgeTX/edgetx/actions/workflows/actions.yml/badge.svg)](https://github.com/EdgeTX/edgetx/actions/workflows/actions.yml)
[![Gitpod ready-to-code](https://img.shields.io/badge/Gitpod-ready--to--code-blue?logo=gitpod)](https://gitpod.io/#https://github.com/edgetx/edgetx/tree/2.11)
[![Conventional Commits](https://img.shields.io/badge/Conventional%20Commits-1.0.0-%23FE5196?logo=conventionalcommits&logoColor=white)](https://conventionalcommits.org)
[![Discord](https://img.shields.io/discord/839849772864503828.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2)](https://discord.gg/wF9wUKnZ6H)
[![Support us on OpenCollective](https://img.shields.io/opencollective/all/edgetx)](https://opencollective.com/edgetx)


<p align="center">
<a href="https://raw.githubusercontent.com/EdgeTX/edgetx.github.io/master/docs/assets/logo.png"><img src="https://raw.githubusercontent.com/EdgeTX/edgetx.github.io/master/docs/assets/logo.png" align="center" height="150" width="150" ></a>

# Welcome to EdgeTX!
**The cutting edge open-source firmware for your R/C radio!**


### About EdgeTX
EdgeTX is the cutting edge of OpenTX. It is the place where innovative ideas and cutting-edge features are developed and field-tested by the enthusiasts of our hobby. EdgeTX is a community project – ideas from the community, developed by the community, and enjoyed by the community! The community will always have a say in what EdgeTX is and what EdgeTX will be in the future. Without community feedback and involvement EdgeTX cannot exist.

### Community
[Discord](https://discord.gg/wF9wUKnZ6H)   

[Facebook](https://www.facebook.com/groups/edgetx)

[Github Discussions](https://github.com/EdgeTX/edgetx/discussions)
  
### Navigation Links

> [Community Guidelines](https://github.com/EdgeTX/edgetx.github.io/wiki/Community-Guidlines)

> [Installation Guide](https://edgetx.gitbook.io/edgetx-user-manual/edgetx-how-to/update-from-opentx-to-edgetx)

> [Installation Video](https://www.youtube.com/watch?v=Y9OvW9XCjOs)

> [FAQ](https://github.com/EdgeTX/edgetx.github.io/wiki/Frequently-Asked-Questions)

> [Reporting Issues / Requesting features](https://github.com/EdgeTX/edgetx/issues/new/choose)

> [Development WIKI](https://github.com/EdgeTX/edgetx/wiki)

> [Lua Documentation Site](https://luadoc.edgetx.org/)
  
> [Flasher Info Page](https://github.com/EdgeTX/flasher) 

> [Flasher Downloads](https://github.com/EdgeTX/flasher/releases)

> [SD Card Info Page](https://github.com/EdgeTX/edgetx-sdcard)

> [SD Card Downloads](https://github.com/EdgeTX/edgetx-sdcard/releases)

> [Sound Packs Info Page](https://github.com/EdgeTX/edgetx-sdcard-sounds)

> [Sound Packs Downloads](https://github.com/EdgeTX/edgetx-sdcard-sounds/releases)

> [EdgeTX Build Environment Docker Images](https://github.com/EdgeTX/build-edgetx)


## Branch `txeditor-wasm/v2.11.7`: WebAssembly simulator modules

This branch builds EdgeTX 2.11.7's
simulator as a WebAssembly module with the C API of EdgeTX 3.0's web simulator
(`targets/simu/simulib.h` there), for a headless host: a tool that boots the radio firmware on a
copy of an SD card and lets it save the card's files in its own format, and a browser simulator.
The modules are GPL-2.0-only like all of EdgeTX: this branch is their source. It starts at the
`v2.11.7` tag; see also `txeditor-wasm/v2.12.4` and `txeditor-wasm/3.0-dev`.

Changes against v2.11.7:

- `simuCreateDefaults()` (backport of 3.0): an empty card gets radio.yml and a first model without
  the STORAGE WARNING alert.
- `radio/src/targets/simu/wasm_api.cpp` and the WASI section of `targets/simu/CMakeLists.txt`:
  the `wasi-module` target, exporting 3.0's names (see the differences at the top of
  `wasm_api.cpp`: no audio, telemetry, trainer or aux serial; radio clock in UTC) plus
  `simuJudgeMarkDirty()` and `simuPer10ms()`; `cmake/toolchain/wasi-threads.cmake` from 3.0.
- 2.11 differs from 3.0 for the host: call `simuPer10ms()` every 10 ms while the radio runs
  (2.11's simulator has no timer task), and the settings path is not an overlay (RADIO/ and
  MODELS/*.yml are read and written there only: give both paths the card's directory, on a copy).
- `tools/build-wasm-judge.sh`: builds the `wasi-module` target per radio and writes a manifest.

Build (one build directory per radio under `build/`, at most `MAX_JOBS` compile jobs, default 2):

```bash
WASI_SDK_PATH=/path/to/wasi-sdk-25.0-x86_64-linux tools/build-wasm-judge.sh -o out tx16s gx12 boxer
```

Needs CMake 3.24+, [WASI SDK 25](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-25)
(`WASI_SDK_PATH`, else `/opt/wasi-sdk` or `~/.cache/wasi-sdk-25.0-*`) and a python3 with jinja2,
pillow, lz4 and pydantic, and the clang Python bindings with a libclang (`LIBCLANG_DIR`, else the newest `/usr/lib/llvm-*/lib`) for `datacopy.inc`. Radio names are those of `tools/build-common.sh`. Each radio gives
`out/<radio>.wasm`; `out/manifest.json` records board, EdgeTX version, branch, commit, size, sha256,
build time, the module's `simu*` exports and its `env` imports.

The module is a wasm32-wasip1-threads reactor: the host provides `env.memory` (shared), WASI
preview 1, `wasi.thread-spawn` (for example `@emnapi/wasi-threads`) and the imports
`simuGetAnalog(idx)` (0..4096, centre 2048) and `simuTrace(text)`; any other `env` function import
may be a no-op. Calls: `simuInit()`, `simuFatfsSetPaths(sdPath, settingsPath)`,
`simuCreateDefaults()` (empty card only), `simuStart(tests, utcOffset)`, then keys, switches, trims,
the LCD (`simuLcdChanged/Copy/Flushed`) and the output readers; `simuStop()` powers the radio off,
which writes the current model and `RADIO/radio.yml`.

## Acknowledgements
Some icon assets provided by [ICONS8](https://icons8.com).</br>
Lua Documentation site powered with the kind support of [GitBook](https://www.gitbook.com).
