#!/bin/bash
# Builds tico-drastic with RetroAchievements. DraStic is a paid, closed app:
# point DRASTIC_APK_DIR at YOUR OWN extracted copy of the Play Store APK
# (com.dsemu.drastic r2.6.0.4a, the arm64-v8a one: it must contain
# lib/arm64-v8a/libdrastic_arm64.so, assets/game_database.xml and
# assets/shaders/). The .nro lands in ./out/drastic/; it embeds the DraStic
# core, so do not redistribute it.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${TICO_BUILD:-$HOME/tico-build}"
APK="${DRASTIC_APK_DIR:?set DRASTIC_APK_DIR to your extracted DraStic APK}"
for f in lib/arm64-v8a/libdrastic_arm64.so assets/game_database.xml assets/shaders/None.dfx; do
  [ -f "$APK/$f" ] || { echo "missing $APK/$f" >&2; exit 1; }
done

mkdir -p "$WORK" && cd "$WORK"
[ -d tico-drasticDS ] || git clone -q --depth 1 https://github.com/ticohq/tico-drasticDS.git
cd tico-drasticDS
git submodule update --init --recursive --depth 1 -q || true
python3 "$REPO/patches/patch_ra.py"
bash "$REPO/drastic/apply.sh" "$PWD"

JOBS=6 DRASTIC_APK_DIR="$APK" SWITCH_DEV_IMAGE=ghcr.io/autorunhq/switch-dev:2026.10.05 \
  bash ./build_tico.sh
mkdir -p "$REPO/out/drastic"
cp tico-drastic.nro "$REPO/out/drastic/"
ls -la "$REPO/out/drastic"
