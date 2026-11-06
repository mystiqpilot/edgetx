#!/bin/bash
# Build EdgeTX simulator WebAssembly modules (wasm32-wasip1-threads) for a
# headless host: a "firmware judge" that boots a radio on a copy of an SD card
# and lets it save its own files, or a browser simulator.
#
# usage: tools/build-wasm-judge.sh [-o OUTDIR] [-B BUILDROOT] RADIO [RADIO...]
#   RADIO      a target name of tools/build-common.sh (tx16s, gx12, boxer, ...)
#   -o OUTDIR  where <radio>.wasm and manifest.json go (default: output/wasm-judge)
#   -B DIR     where the per-radio build directories wasm-judge-<radio>/ and their
#              logs wasm-judge-<radio>.log go (default: build/)
#
# environment:
#   WASI_SDK_PATH  WASI SDK 25 (else /opt/wasi-sdk, else ~/.cache/wasi-sdk-25.0-*,
#                  else .cache/fetchcontent/wasisdk-src of this tree)
#   MAX_JOBS       parallel compile jobs (default 2)
#   PYTHON         python3 with jinja2, pillow, lz4, pydantic (EdgeTX 2.11 also
#                  needs the clang bindings and a libclang: LIBCLANG_DIR, else the
#                  newest /usr/lib/llvm-*/lib with libclang.so)
#   EDGETX_VERSION_SUFFIX  as for every EdgeTX build (default "selfbuild")
#
# The manifest records per radio: board, EdgeTX version, branch, commit, file,
# sha256, size, build time, WASI SDK version, the module's simu* exports and
# its "env" imports. When run inside a memory-capped systemd scope the script
# prints that cgroup's memory.peak at the end.

set -o pipefail

SRCDIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${SRCDIR}/output/wasm-judge"
BUILDROOT="${SRCDIR}/build"
MAX_JOBS="${MAX_JOBS:-2}"

usage() { sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while getopts "o:B:h" opt; do
  case $opt in
    o) OUTDIR="$OPTARG" ;;
    B) BUILDROOT="$OPTARG" ;;
    h) usage 0 ;;
    *) usage 1 ;;
  esac
done
shift $((OPTIND - 1))
[[ $# -ge 1 ]] || usage 1
RADIOS=("$@")

die() { echo "error: $*" >&2; exit 1; }

# -- WASI SDK --
find_wasi_sdk() {
  local c
  for c in "${WASI_SDK_PATH:-}" /opt/wasi-sdk \
           "${XDG_CACHE_HOME:-$HOME/.cache}"/wasi-sdk-25.0-* \
           "${SRCDIR}/.cache/fetchcontent/wasisdk-src"; do
    if [[ -n "$c" && -f "$c/share/cmake/wasi-sdk-pthread.cmake" ]]; then
      echo "$c"; return 0
    fi
  done
  return 1
}
WASI_SDK_PATH="$(find_wasi_sdk)" || die "WASI SDK 25 not found: set WASI_SDK_PATH
  (https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-25)"
export WASI_SDK_PATH
WASI_SDK_VERSION="$(head -n1 "$WASI_SDK_PATH/VERSION" 2>/dev/null)"
echo "WASI SDK ${WASI_SDK_VERSION:-?} in $WASI_SDK_PATH"

# -- Python (EdgeTX 2.11 also runs radio/util/generate_datacopy.py: clang bindings + libclang) --
PYTHON_EXE=""
for want in "jinja2, PIL, lz4, pydantic, clang.cindex" "jinja2, PIL, lz4, pydantic"; do
  for c in "${PYTHON:-}" python3 /usr/bin/python3; do
    [[ -n "$c" ]] || continue
    c="$(command -v "$c")" || continue
    if "$c" -I -c "import ${want}" 2>/dev/null; then PYTHON_EXE="$c"; break 2; fi
  done
done
[[ -n "$PYTHON_EXE" ]] || die "no python3 with jinja2, PIL, lz4, pydantic (set PYTHON)"
echo "Python: $PYTHON_EXE"

libclang_ok() {
  "$PYTHON_EXE" -I -c "import clang.cindex as c; c.Config.set_compatibility_check(False); c.Index.create()" 2>/dev/null
}
if "$PYTHON_EXE" -I -c "import clang.cindex" 2>/dev/null; then
  if [[ -n "${LIBCLANG_DIR:-}" ]]; then
    export LD_LIBRARY_PATH="${LIBCLANG_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  elif ! libclang_ok; then
    d="$(ls -d /usr/lib/llvm-*/lib 2>/dev/null | sort -V | while read -r l; do
           [[ -e "$l/libclang.so" ]] && echo "$l"; done | tail -n1)"
    [[ -n "$d" ]] && export LD_LIBRARY_PATH="${d}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  fi
  if libclang_ok; then echo "libclang: ${LD_LIBRARY_PATH:-default search}"
  else echo "warning: libclang not loadable (set LIBCLANG_DIR); EdgeTX 2.11 builds need it"; fi
else
  echo "warning: no clang Python bindings; EdgeTX 2.11 builds need them (datacopy.inc)"
fi

# -- this tree --
. "$SRCDIR/tools/build-common.sh"
ver_part() { sed -n "s/^set(VERSION_$1 \"\{0,1\}\([0-9]*\)\"\{0,1\})/\1/p" "$SRCDIR/CMakeLists.txt" | head -n1; }
EDGETX_VERSION="$(ver_part MAJOR).$(ver_part MINOR).$(ver_part REVISION)"
GIT_BRANCH="$(git -C "$SRCDIR" rev-parse --abbrev-ref HEAD)"
GIT_COMMIT="$(git -C "$SRCDIR" rev-parse HEAD)"
GIT_DIRTY=false
[[ -n "$(git -C "$SRCDIR" status --porcelain --untracked-files=no)" ]] && GIT_DIRTY=true
echo "EdgeTX $EDGETX_VERSION, $GIT_BRANCH @ ${GIT_COMMIT:0:10}$([[ $GIT_DIRTY == true ]] && echo ' (uncommitted changes)')"

mkdir -p "$OUTDIR" || die "cannot create $OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"

build_radio() {
  local radio="$1" bld="${BUILDROOT}/wasm-judge-$1" log start
  log="${bld}.log"
  mkdir -p "$BUILDROOT" || return 1
  BUILD_OPTIONS=""
  get_target_build_options "$radio" > "$log" 2>&1 || { echo "unknown radio: $radio"; return 1; }
  start=$(date +%s)
  echo "== $radio: $BUILD_OPTIONS (log: $log)"
  # shellcheck disable=SC2086
  cmake --fresh -S "$SRCDIR" -B "$bld" \
    --toolchain "$SRCDIR/cmake/toolchain/wasi-threads.cmake" \
    -DWASI_SDK_PREFIX="$WASI_SDK_PATH" -DCMAKE_MODULE_PATH="$WASI_SDK_PATH/share/cmake/" \
    -DCMAKE_BUILD_TYPE=Release -DEdgeTX_SUPERBUILD=OFF -DNATIVE_BUILD=ON \
    -DDISABLE_COMPANION=ON -DPython3_EXECUTABLE="$PYTHON_EXE" -DPYTHON_EXECUTABLE="$PYTHON_EXE" \
    -DCMAKE_MESSAGE_LOG_LEVEL=WARNING -Wno-dev $BUILD_OPTIONS >> "$log" 2>&1 \
    || { echo "configure failed:"; tail -n 30 "$log"; return 1; }
  cmake --build "$bld" --target wasi-module --parallel "$MAX_JOBS" >> "$log" 2>&1 \
    || { echo "build failed:"; grep -m 20 -E "error|Error" "$log"; return 1; }
  local wasm
  wasm="$(ls "$bld"/edgetx-*-simulator.wasm 2>/dev/null | head -n1)"
  [[ -f "$wasm" ]] || { echo "no edgetx-*-simulator.wasm in $bld"; return 1; }
  cp "$wasm" "$OUTDIR/${radio}.wasm" || return 1
  local seconds=$(( $(date +%s) - start ))
  "$PYTHON_EXE" -I - "$OUTDIR" "$radio" "$(basename "$wasm")" "$seconds" <<PYEOF || return 1
import hashlib, json, os, sys, datetime
outdir, radio, built_name, seconds = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
path = os.path.join(outdir, radio + ".wasm")
data = open(path, "rb").read()

def wasm_interface(b):
    pos = 8
    def u32():
        nonlocal pos
        r = s = 0
        while True:
            x = b[pos]; pos += 1
            r |= (x & 0x7f) << s
            if not x & 0x80: return r
            s += 7
    def name():
        nonlocal pos
        n = u32(); v = b[pos:pos + n].decode(); pos += n; return v
    imports, exports = [], []
    while pos < len(b):
        sid = b[pos]; pos += 1
        size = u32(); end = pos + size
        if sid == 2:
            for _ in range(u32()):
                mod, field, kind = name(), name(), b[pos]; pos += 1
                if kind == 0: u32()
                elif kind == 1: pos += 1; fl = u32(); u32(); fl & 1 and u32()
                elif kind == 2:
                    fl = u32(); u32()
                    if fl & 1: u32()
                elif kind == 3: pos += 2
                elif kind == 4: pos += 1; u32()
                imports.append((mod, field, kind))
        elif sid == 7:
            for _ in range(u32()):
                field, kind = name(), b[pos]; pos += 1; u32()
                exports.append((field, kind))
        pos = end
    return imports, exports

imports, exports = wasm_interface(data)
manifest_path = os.path.join(outdir, "manifest.json")
try:
    manifest = json.load(open(manifest_path))
except (OSError, ValueError):
    manifest = {}
manifest["format"] = 1
manifest.setdefault("modules", {})[radio] = {
    "board": radio,
    "flavour": built_name[len("edgetx-"):-len("-simulator.wasm")],
    "file": radio + ".wasm",
    "edgetxVersion": "${EDGETX_VERSION}",
    "versionSuffix": os.environ.get("EDGETX_VERSION_SUFFIX", "selfbuild"),
    "branch": "${GIT_BRANCH}",
    "commit": "${GIT_COMMIT}",
    "uncommittedChanges": ${GIT_DIRTY^},
    "size": len(data),
    "sha256": hashlib.sha256(data).hexdigest(),
    "builtAt": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "buildSeconds": seconds,
    "wasiSdk": "${WASI_SDK_VERSION}",
    "cmakeOptions": "${BUILD_OPTIONS}",
    "exports": sorted(f for f, k in exports if k == 0 and f.startswith("simu")),
    "envImports": sorted(f for m, f, k in imports if m == "env" and k == 0),
}
manifest["modules"] = dict(sorted(manifest["modules"].items()))
with open(manifest_path + ".tmp", "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
os.replace(manifest_path + ".tmp", manifest_path)
m = manifest["modules"][radio]
print(f"   {m['file']}: {m['size']} bytes, sha256 {m['sha256'][:16]}..., {seconds} s, "
      f"{len(m['exports'])} simu exports, env imports: {', '.join(m['envImports']) or '-'}")
PYEOF
}

failed=()
for radio in "${RADIOS[@]}"; do
  build_radio "$(echo "$radio" | tr '[:upper:]' '[:lower:]')" || failed+=("$radio")
done

cg="/sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup 2>/dev/null)"
[[ -r "$cg/memory.peak" ]] && echo "cgroup memory.peak: $(cat "$cg/memory.peak") bytes"

if [[ ${#failed[@]} -gt 0 ]]; then
  echo "failed: ${failed[*]}"
  exit 1
fi
echo "manifest: $OUTDIR/manifest.json"
