#!/bin/bash
# Clones a tico core, applies the session_override.json fallback and builds it
# in Docker. The .nro lands in ./out/<core>/.
#   scripts/build_core.sh mupen64plus|mgba|duckstation|dolphin
# Needs git, python3 and Docker. Keep WSL at 8 GB of RAM for Dolphin.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${TICO_BUILD:-$HOME/tico-build}"
CPUS="${CPUS:-0-5}"   # parallel jobs = CPUs given to the container
IMAGE=ghcr.io/autorunhq/switch-dev:2026.10.05
LEGACY=ghcr.io/danfromtico/switch-dev-legacy:2026.10.05.1

core="${1:?usage: build_core.sh mupen64plus|mgba|duckstation|dolphin}"
case "$core" in
  mupen64plus) script=build_mupen64plus_nro.sh ;;
  mgba)        script=build_mgba_nro.sh ;;
  duckstation) script=build_duckstation_nro.sh ;;
  dolphin)     script=build_dolphin_standalone_nro.sh ;;
  *) echo "unknown core: $core" >&2; exit 1 ;;
esac

mkdir -p "$WORK" && cd "$WORK"
[ -d "tico-$core" ] || git clone -q --depth 1 "https://github.com/ticohq/tico-$core.git"
cd "tico-$core"
git submodule update --init --recursive --depth 1 -q || true
python3 "$REPO/patches/patch_ra.py"

if [ "$core" = duckstation ]; then
  # Its build expects libusbhsfs prebuilt under tico/frontend/deps (not in git)
  D="$PWD/tico/frontend/deps/usbhsfs"
  mkdir -p "$D/lib" "$D/include"
  docker run --rm -v "$D:/out" "$LEGACY" bash -c \
    'cp /opt/devkitpro/portlibs/switch/lib/libusbhsfs.a /opt/devkitpro/portlibs/switch/lib/libusbntfs.a /out/lib/ &&
     cp /opt/devkitpro/portlibs/switch/include/usbhsfs.h /out/include/'
fi

if [ "$core" = mupen64plus ]; then
  # Runs itself in the legacy (OpenGL) image; BUILD_JOBS keeps WSL's RAM in check
  BUILD_JOBS=4 bash "./$script"
else
  docker run --rm --cpuset-cpus="$CPUS" -v "$PWD:/work" -w /work "$IMAGE" bash "./$script"
fi

mkdir -p "$REPO/out/$core"
find . -path ./.git -prune -o -name "tico-$core.nro" -newer "$REPO/patches/patch_ra.py" -print \
  -exec cp {} "$REPO/out/$core/" \;
ls -la "$REPO/out/$core"
