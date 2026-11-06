[![GitHub release (latest by date)](https://img.shields.io/github/v/release/Edgetx/edgetx)](https://github.com/EdgeTX/edgetx/releases/latest)
[![GitHub all releases](https://img.shields.io/github/downloads/EdgeTX/edgetx/total)](https://github.com/EdgeTX/edgetx/releases)
[![GitHub license](https://img.shields.io/github/license/Edgetx/edgetx)](https://github.com/EdgeTX/edgetx/blob/main/LICENSE)
[![Commit Tests](https://github.com/EdgeTX/edgetx/actions/workflows/build_fw.yml/badge.svg)](https://github.com/EdgeTX/edgetx/actions/workflows/build_fw.yml)
[![GitHub CodesSpaces ready-to-code](https://img.shields.io/badge/GitHub%20CodesSpaces-ready--to--code-blue?logo=github)](https://codespaces.new/EdgeTX/edgetx)
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
- [Discord](https://discord.gg/wF9wUKnZ6H)   

- [Facebook](https://www.facebook.com/groups/edgetx)

- [Github Discussions](https://github.com/EdgeTX/edgetx/discussions)
  
### Navigation Links

- [Community Guidelines](https://github.com/EdgeTX/edgetx.github.io/wiki/Community-Guidlines)

- [Installation Guide](https://manual.edgetx.org/installing-and-updating-edgetx/update-from-opentx-to-edgetx)

- [Installation Video](https://www.youtube.com/watch?v=Y9OvW9XCjOs)

- [Reporting Issues / Requesting features](https://github.com/EdgeTX/edgetx/issues/new/choose)

- [Lua Documentation Site](https://luadoc.edgetx.org/)
  
- Buddy: [Info](https://github.com/EdgeTX/buddy) - [Downloads](https://github.com/EdgeTX/buddy/releases) 

- SD Card: [Info](https://github.com/EdgeTX/edgetx-sdcard) - [Downloads](https://github.com/EdgeTX/edgetx-sdcard/releases)

- Sound Packs:  [Info](https://github.com/EdgeTX/edgetx-sdcard-sounds) - [Downloads](https://github.com/EdgeTX/edgetx-sdcard-sounds/releases)

- [Developer Documentation](https://edgetx.org/edgetx/latest/) - [Docker Build Environment](https://github.com/EdgeTX/build-edgetx)


## Branch `txeditor-wasm/3.0-dev`: WebAssembly simulator modules

This branch builds EdgeTX's
WebAssembly simulator (`targets/simu/simulib.h`) for a headless host: a tool that boots the radio
firmware on a copy of an SD card and lets it save the card's files in its own format, and a
browser simulator. It is based on the fork's `channel-stream` branch (EdgeTX 3.0-dev with the
USB-VCP channel stream mode). The modules are GPL-2.0-only like all of EdgeTX: this branch is
their source. Sibling branches `txeditor-wasm/v2.11.7` and `txeditor-wasm/v2.12.4` export the
same API from those releases.

Changes against the base:

- `simuJudgeMarkDirty()` (`targets/simu/simulib.h`): marks the current model and the radio
  settings as changed, so that `simuStop()` writes both, also for a model whose trims are all
  disabled (setting a trim to its own value does the same otherwise).
- `tools/build-wasm-judge.sh`: builds the `wasi-module` target per radio and writes a manifest.

Build (one build directory per radio under `build/`, at most `MAX_JOBS` compile jobs, default 2):

```bash
WASI_SDK_PATH=/path/to/wasi-sdk-25.0-x86_64-linux tools/build-wasm-judge.sh -o out tx16s gx12 boxer
```

Needs CMake 3.24+, [WASI SDK 25](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-25)
(`WASI_SDK_PATH`, else `/opt/wasi-sdk` or `~/.cache/wasi-sdk-25.0-*`) and a python3 with jinja2,
pillow, lz4 and pydantic. Radio names are those of `tools/build-common.sh`. Each radio gives
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
