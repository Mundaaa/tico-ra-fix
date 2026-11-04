#!/bin/bash
# Adds RetroAchievements (tico_ra.cpp + rcheevos) to a tico-drasticDS checkout.
#   drastic/apply.sh <path to tico-drasticDS>
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
D="${1:?usage: apply.sh <tico-drasticDS checkout>}"
cd "$D"

# rcheevos at the commit the other tico cores use; rc_libretro needs libretro.h
RCHEEVOS_COMMIT=e9ca3694c862b61235595176dac4b22677848c93
if [ ! -f third_party/rcheevos/include/rc_client.h ]; then
  rm -rf third_party/rcheevos
  git clone -q https://github.com/RetroAchievements/rcheevos.git third_party/rcheevos
  git -C third_party/rcheevos checkout -q "$RCHEEVOS_COMMIT"
  rm -rf third_party/rcheevos/.git third_party/rcheevos/test third_party/rcheevos/validator
  rm -f third_party/rcheevos/src/rc_libretro.c
fi

cp "$HERE/tico_ra.h" "$HERE/tico_ra.cpp" source/tico/

python3 - <<'EOF'
import pathlib
def sub(path, a, b):
    p = pathlib.Path(path); t = p.read_text()
    if b in t: return
    assert t.count(a) == 1, (path, a)
    p.write_text(t.replace(a, b)); print('patched', path)

sub('Makefile',
"INCLUDES\t+=\tthird_party/imgui third_party/imgui/backends third_party/tico\n",
"INCLUDES\t+=\tthird_party/imgui third_party/imgui/backends third_party/tico\n"
"# RetroAchievements (source/tico/tico_ra.cpp)\n"
"SOURCES\t\t+=\tthird_party/rcheevos/src third_party/rcheevos/src/rcheevos \\\n"
"\t\t\tthird_party/rcheevos/src/rhash third_party/rcheevos/src/rapi\n"
"INCLUDES\t+=\tthird_party/rcheevos/include\n")
sub('Makefile', "-DDRASTIC_TICO -DIMGUI_USER_CONFIG", "-DDRASTIC_TICO -DRC_CLIENT_SUPPORTS_HASH -DIMGUI_USER_CONFIG")
sub('Makefile', "LIBS\t+= -lsodium\n", "LIBS\t+= -lsodium -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz\n")

sub('source/main.c', '#include "tico/tico_menu.h"\n', '#include "tico/tico_menu.h"\n#include "tico/tico_ra.h"\n')
sub('source/main.c',
"""    fatal_error("Drastic does not recognize this ROM:\\n%s", runtime.rom_path);
""",
"""    fatal_error("Drastic does not recognize this ROM:\\n%s", runtime.rom_path);
#ifdef DRASTIC_TICO
  tico_ra_start(prepared_rom_path);
#endif
""")
sub('source/main.c',
"""    tico_playtime_frame(!drastic_menu_is_open(menu));
""",
"""    tico_playtime_frame(!drastic_menu_is_open(menu));
    tico_ra_frame(!drastic_menu_is_open(menu));
""")
sub('source/main.c',
"""  tico_playtime_flush(); /* the game's last seconds */
""",
"""  tico_playtime_flush(); /* the game's last seconds */
  tico_ra_stop();
""")

p = pathlib.Path('source/libc_shim.c'); t = p.read_text()
if 'ashmem_region_at' not in t:
    t += """
/* For tico_ra.cpp: the backing memory of the index-th ashmem slot (NULL when
 * free); -1 past the last slot. */
int ashmem_region_at(int index, void **raw, size_t *size) {
  if (index < 0 || index >= MAX_ASHMEM)
    return -1;
  mutexLock(&g_ashmem_lock);
  *raw = g_ashmem[index].used == 1 ? g_ashmem[index].raw : NULL;
  *size = g_ashmem[index].used == 1 ? g_ashmem[index].size : 0;
  mutexUnlock(&g_ashmem_lock);
  return 0;
}
"""
    p.write_text(t); print('patched source/libc_shim.c')
EOF
