#!/bin/bash
# Clones a tico core, applies the session_override.json fallback and builds it
# in Docker. The .nro lands in ./out/<core>/.
#   scripts/build_core.sh mupen64plus|mgba|duckstation|dolphin|ppsspp|genesisplusgx|flycast|snes9x|stella
# Needs git, python3 and Docker. Keep WSL at 8 GB of RAM for Dolphin.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${TICO_BUILD:-$HOME/tico-build}"
CPUS="${CPUS:-0-5}"   # parallel jobs = CPUs given to the container
IMAGE=ghcr.io/autorunhq/switch-dev:2026.10.05
LEGACY=ghcr.io/danfromtico/switch-dev-legacy:2026.10.05.1

core="${1:?usage: build_core.sh mupen64plus|mgba|duckstation|dolphin|ppsspp|genesisplusgx|flycast|snes9x|stella}"
case "$core" in
  mupen64plus) script=build_mupen64plus_nro.sh ;;
  mgba)        script=build_mgba_nro.sh ;;
  duckstation) script=build_duckstation_nro.sh ;;
  dolphin)     script=build_dolphin_standalone_nro.sh ;;
  ppsspp)      script=build_ppsspp_tico_nro.sh ;;
  genesisplusgx) script=build_genesis_plus_gx_nro.sh ;;
  flycast)     script=build_flycast_nro.sh ;;
  snes9x)      script=build_snes9x_nro.sh ;;
  stella)      script=build_stella_nro.sh ;;
  *) echo "unknown core: $core" >&2; exit 1 ;;
esac

mkdir -p "$WORK" && cd "$WORK"
[ -d "tico-$core" ] || git clone -q --depth 1 "https://github.com/ticohq/tico-$core.git"
cd "tico-$core"
git submodule update --init --recursive --depth 1 -q || true
python3 "$REPO/patches/patch_ra.py"

D=""
[ "$core" = duckstation ] && D="$PWD/tico/frontend/deps/usbhsfs"
[ "$core" = genesisplusgx ] && D="$PWD/tico/deps/usbhsfs"
if [ -n "$D" ] && [ ! -f "$D/lib/libusbhsfs.a" ]; then
  # Their builds expect libusbhsfs prebuilt under tico/(frontend/)deps (not in git)
  mkdir -p "$D/lib" "$D/include"
  docker run --rm -v "$D:/out" "$LEGACY" bash -c \
    'cp /opt/devkitpro/portlibs/switch/lib/libusbhsfs.a /opt/devkitpro/portlibs/switch/lib/libusbntfs.a /out/lib/ &&
     cp /opt/devkitpro/portlibs/switch/include/usbhsfs.h /out/include/'
fi

if [ "$core" = mupen64plus ]; then
  # Runs itself in the legacy (OpenGL) image; BUILD_JOBS keeps WSL's RAM in check
  BUILD_JOBS=4 bash "./$script"
elif [ "$core" = ppsspp ]; then
  # This image's toolchain sets neither UNIX nor __unix, and ships no Switch
  # FFmpeg where PPSSPP looks: build FFmpeg into ffmpeg/switch_build, give
  # glslang its OSDependent library, include <netinet/in.h> for aemu_postoffice
  # and leave UPnP out (no sys/un.h).
  grep -q NINTENDO_SWITCH ext/glslang/glslang/CMakeLists.txt ||
    sed -i 's/elseif(UNIX OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Fuchsia")/elseif(UNIX OR NINTENDO_SWITCH OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Fuchsia")/' \
      ext/glslang/glslang/CMakeLists.txt
  sed 's|-DUSE_DISCORD=OFF|-DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF|' "$script" > build_ppsspp_ra.sh
  docker run --rm --cpuset-cpus="$CPUS" -v "$PWD:/work" -w /work "$IMAGE" bash -c '
    set -e
    git config --global --add safe.directory "*"
    if [ ! -f ffmpeg/switch_build/lib/libavcodec.a ]; then
      sed "s|--prefix=\$PORTLIBS_PREFIX|--prefix=/work/ffmpeg/switch_build|" ffmpeg/switch.sh > /tmp/switch.sh
      (cd ffmpeg && bash /tmp/switch.sh)
    fi
    export FFMPEG_DIR=/work/ffmpeg/switch_build CFLAGS="-include netinet/in.h"
    bash ./build_ppsspp_ra.sh'
  rm -f build_ppsspp_ra.sh
else
  docker run --rm --cpuset-cpus="$CPUS" -v "$PWD:/work" -w /work "$IMAGE" bash "./$script"
fi

mkdir -p "$REPO/out/$core"
find . -path ./.git -prune -o -name "tico-$core.nro" -newer "$REPO/patches/patch_ra.py" -print \
  -exec cp {} "$REPO/out/$core/" \;
ls -la "$REPO/out/$core"
