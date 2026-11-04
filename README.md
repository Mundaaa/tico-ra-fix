# tico RetroAchievements fix (tico 0.8.0)

*[Version française plus bas](#français)*

Since **tico 0.8.0**, RetroAchievements no longer work: games show their
achievement list, but nothing unlocks and nothing reaches your RA profile.
This repository is a community workaround until tico itself is fixed
(see [ticohq/tico#271](https://github.com/ticohq/tico/issues/271)).

## What is broken

In 0.8.0 each Switch account gets its own RA account, and `tico.nro` hands the
core a sealed session (`sdmc:/tico/run/session.bin`) with a sign-in token. On
affected setups that session arrives **without the RA fields**, so every core
logs:

```
RA: Session opened (Enabled: 0, User: )
```

`tico.nro` is closed source, so it cannot be fixed here. The cores are GPL
forks ([github.com/ticohq](https://github.com/ticohq)), so they can be rebuilt.

## How this works

The cores are rebuilt with a small fallback in `TicoSession.h`
([patches/patch_ra.py](patches/patch_ra.py)): when a self-built core cannot open
tico's sealed session (it does not have tico's build secret), it reads the same
fields from a plain file you write yourself, `sdmc:/tico/session_override.json`.
That file holds your RA username and connect token, plus your account's
save folders so saves and states stay where tico puts them.

| Console | Core | Status |
|---|---|---|
| N64 | `tico-mupen64plus` | tested, unlocks work |
| GBA / GB / GBC | `tico-mgba` | tested, unlocks work |
| PS1 | `tico-duckstation` | tested, unlocks work |
| GameCube / Wii | `tico-dolphin` | tested, unlocks work |
| PS2 | NetherSX2 | tested, unlocks work: no rebuild, fill its ini (below) |
| DS | `tico-drasticDS` | **experimental**: login, game and RAM found; an unlock not confirmed yet |
| 3DS, others | | not covered |

Side effects of a self-built core: tico does not count its playtime, Discord
presence is off, and a core update from the tico Store puts the official
build back (rebuild after updating).

## Quick install (prebuilt cores)

No PC build needed: the patched cores are attached to the
[latest release](../../releases/latest).

1. **Download** from the release the `.nro` of the consoles you play, and
   [config/session_override.example.json](config/session_override.example.json).
2. **Put the SD card in your PC** (a card reader is far more reliable than
   MTP for large files).
3. **Back up each original core**: copy
   `sdmc:/tico/modules/<core>/tico-<core>.nro` somewhere on your PC.
4. **Replace it** with the downloaded `.nro`, same folder, same name. Touch
   nothing else in the module folder.

   | Console | Release file | Folder on the SD card |
   |---|---|---|
   | N64 | `tico-mupen64plus.nro` | `tico/modules/mupen64plus/` |
   | GBA / GB / GBC | `tico-mgba.nro` | `tico/modules/mgba/` |
   | PS1 | `tico-duckstation.nro` | `tico/modules/duckstation/` |
   | GameCube / Wii | `tico-dolphin.nro` | `tico/modules/dolphin/` |

5. **Write your `session_override.json`** (see
   [step 2 below](#2-write-session_overridejson)): your Switch account id,
   your RA username and your RA **token**, then copy it to
   `sdmc:/tico/session_override.json`.
6. **PS2**: no core to replace, fill in NetherSX2's ini ([see below](#ps2-nethersx2)).
7. Put the SD card back, launch a game: the **"RA Playing"** toast appears
   within a few seconds.

**Do not update these cores from the tico Store**: an update puts the official
build back, without the fix. To undo, put your original `.nro` back and delete
`session_override.json`.

The N64 core is built with the legacy OpenGL image: only the GLideN64
renderer is in it. If you picked a Vulkan renderer (paraLLEl) in the N64
settings, switch back to GLideN64.

The release cores are built unmodified from ticohq's sources plus
[patches/patch_ra.py](patches/patch_ra.py), at the commits listed in the
release notes (GPL: that is their corresponding source). There is no DS build
in the release: it embeds the paid DraStic core, so everyone has to build it
from their own APK ([see below](#ds-drastic-experimental)).

## Build it yourself

### Requirements

- A PC with **Docker** and **WSL 2** (Ubuntu) or Linux, plus `git` and `python3`.
- For Dolphin: give WSL at least **8 GB of RAM** (`memory=8GB` in
  `%USERPROFILE%\.wslconfig`, then `wsl --shutdown`). With 3 GB the build
  freezes WSL.

### 1. Build the cores

From WSL, in this repository:

```bash
bash scripts/build_core.sh mupen64plus
bash scripts/build_core.sh mgba
bash scripts/build_core.sh duckstation
bash scripts/build_core.sh dolphin
```

Each `.nro` lands in `out/<core>/`. The first build pulls the Docker images
(several GB). Dolphin takes 15 to 60 minutes; the others a few minutes.

### 2. Write `session_override.json`

1. Copy [config/session_override.example.json](config/session_override.example.json)
   to `session_override.json`.
2. Replace the user id (32 hex digits) with yours. It is the folder name under
   `sdmc:/tico/saves/users/`. Put it in `user_id`, `saves_path` and `states_path`.
3. Fill in `ra_username` and `ra_password`, then trade the password for a token,
   so that no password stays on the SD card:
   ```
   powershell -ExecutionPolicy Bypass -File tools\get_ra_token.ps1 -Json session_override.json
   ```
   The script contacts only retroachievements.org, writes `ra_token` and
   deletes `ra_password`. mupen64plus and mGBA also accept `ra_password`, but
   DuckStation and Dolphin need the token.

**Never share this file or commit it**: the token logs into your RA account.

### 3. Install

On the SD card (back up each original first):

| File | Goes to |
|---|---|
| `session_override.json` | `sdmc:/tico/session_override.json` |
| `out/mupen64plus/tico-mupen64plus.nro` | `sdmc:/tico/modules/mupen64plus/` |
| `out/mgba/tico-mgba.nro` | `sdmc:/tico/modules/mgba/` |
| `out/duckstation/tico-duckstation.nro` | `sdmc:/tico/modules/duckstation/` |
| `out/dolphin/tico-dolphin.nro` | `sdmc:/tico/modules/dolphin/` |

Replace only the `.nro`, not the rest of the module folder. Launch a game: the
"RA Playing" toast shows up, and the core's log in `sdmc:/tico/debug/` reads
`Enabled: 1`.

To undo: put the original `.nro` back and delete `session_override.json`.

## PS2 (NetherSX2)

NetherSX2 never took its account from tico: it reads it from its own ini, and
switches achievements off at boot when the username or token is empty. In
`sdmc:/tico/system/nethersx2/nethersx2.ini` set:

```ini
Achievements/Enabled = true
Achievements/Username = YOUR_RA_USERNAME
Achievements/Token = YOUR_RA_TOKEN
```

The token is the `ra_token` from step 2. The ini only exists once a PS2 game
has been launched once. NetherSX2 on Switch is Casual only.

## DS (DraStic), experimental

DraStic is a closed Android core with no memory API, so
[drastic/tico_ra.cpp](drastic/tico_ra.cpp) adds RetroAchievements to tico's
host around it:

- it finds the DS main RAM in the process memory by the cartridge header that
  the DS boot copies to `0x027FFE00` (offset `0x3FFE00` of the 4 MiB main RAM),
  checked against the chip id stored at `0x027FF800` and `0x027FFC00`;
- it runs rcheevos (the same commit as the other tico cores) from the host's
  frame loop, with its HTTP worker on a libnx thread (the host installs
  Android-style TLS, so `std::thread` crashes), opening the Switch sockets
  itself (the host only does it for SMB game folders);
- it stops the worker and closes the sockets before the core is torn down:
  hbloader runs tico again in the same process, and anything left running
  crashes tico and Atmosphère.

It logs every step to `sdmc:/tico/debug/drastic_ra.txt`. Casual mode only.

DraStic is a **paid app**. The build needs **your own** APK
(`com.dsemu.drastic`, r2.6.0.4a, arm64-v8a: extract it from a 64-bit Android
device signed in with the Google account that bought it). Copies named
"Drastic DS" from APK sites are often unrelated clones or installers. The built
`.nro` embeds the DraStic core: **do not redistribute it**.

```bash
DRASTIC_APK_DIR=/path/to/extracted/apk bash scripts/build_drastic.sh
```

Then install `out/drastic/tico-drastic.nro` into `sdmc:/tico/modules/drastic/`.

## Troubleshooting

| Log line | Meaning |
|---|---|
| `RA: Session missing` | no `sdmc:/tico/session_override.json`, or it is not valid JSON |
| `Enabled: 0` | `ra_enabled` false, or no `ra_token` (DuckStation/Dolphin) |
| `Login Failed` / `login failed` | wrong or expired token: run `get_ra_token.ps1` again |
| `HTTP error Couldn't connect` (DS) | no network on the console |
| `DS RAM not found yet` (DS) | the RAM search failed: please open an issue with the log |

## Credits

The cores are tico's GPL forks by ticohq, built from their own scripts;
rcheevos by RetroAchievements; DraStic by Exophase. The patches here are
offered under the licence of the files they change (GPL-2.0-or-later for the
tico cores).

---

## Français

Depuis **tico 0.8.0**, les RetroAchievements ne fonctionnent plus : les succès
s'affichent mais ne se débloquent pas. `tico.nro` (fermé) transmet aux
émulateurs une session sans le compte RA. Ce dépôt recompile les émulateurs
(libres, sous GPL) avec un repli : s'ils ne peuvent pas ouvrir la session de
tico, ils lisent ton compte dans un fichier que tu écris toi-même,
`sdmc:/tico/session_override.json`.

### Installation rapide (émulateurs déjà compilés)

Pas besoin de compiler : les émulateurs corrigés sont dans la
[dernière release](../../releases/latest).

1. **Télécharge** dans la release le `.nro` des consoles auxquelles tu joues,
   ainsi que [config/session_override.example.json](config/session_override.example.json).
2. **Branche la carte SD au PC**, de préférence avec un lecteur de carte :
   c'est bien plus fiable que le MTP pour les gros fichiers.
3. **Sauvegarde chaque émulateur d'origine** : copie
   `tico/modules/<émulateur>/tico-<émulateur>.nro` sur ton PC.
4. **Remplace-le** par le `.nro` téléchargé, dans le même dossier et sous le
   même nom. Ne touche à rien d'autre dans le dossier du module.

   | Console | Fichier de la release | Dossier sur la SD |
   |---|---|---|
   | N64 | `tico-mupen64plus.nro` | `tico/modules/mupen64plus/` |
   | GBA / GB / GBC | `tico-mgba.nro` | `tico/modules/mgba/` |
   | PS1 | `tico-duckstation.nro` | `tico/modules/duckstation/` |
   | GameCube / Wii | `tico-dolphin.nro` | `tico/modules/dolphin/` |

5. **Prépare ton `session_override.json`** :
   - renomme `session_override.example.json` en `session_override.json` ;
   - remplace les trois `0123456789abcdef0123456789abcdef` par ton
     identifiant de compte Switch : c'est le nom du dossier (32 caractères)
     dans `tico/saves/users/` sur ta SD ;
   - mets ton pseudo et ton mot de passe RetroAchievements ;
   - lance `tools\get_ra_token.ps1` (clic droit → « Exécuter avec
     PowerShell » ; il te demande le chemin du fichier) : il remplace ton
     mot de passe par un jeton, pour que ton mot de passe ne reste pas sur la
     SD ;
   - copie le fichier dans `tico/` à la racine de la SD.
6. **PS2** : rien à remplacer. Lance d'abord un jeu PS2 une fois (le fichier
   n'existe qu'après), puis dans `tico/system/nethersx2/nethersx2.ini`,
   mets `Achievements/Enabled = true`, ton pseudo dans
   `Achievements/Username` et le jeton (`ra_token` de ton JSON) dans
   `Achievements/Token`.
7. Remets la SD dans la Switch et lance un jeu : la notification
   **« RA Playing »** apparaît au bout de quelques secondes.

**Ne mets pas ces émulateurs à jour depuis le tico Store** : une mise à jour
remet la version officielle, sans le correctif. Pour revenir en arrière,
remets les `.nro` d'origine et supprime `session_override.json`.

N64 : cette version n'a que le moteur graphique GLideN64. Si tu avais choisi
un moteur Vulkan (paraLLEl) dans les réglages N64, repasse sur GLideN64.

DS : pas de version compilée dans la release, car elle contiendrait le cœur
payant de DraStic. Il faut la compiler soi-même avec son propre APK (voir la
partie anglaise).

### Compiler soi-même, en bref

1. Compiler : `bash scripts/build_core.sh mupen64plus` (puis `mgba`,
   `duckstation`, `dolphin`). Il faut Docker, WSL 2 et 8 Go de RAM pour WSL
   (Dolphin).
2. Copier `config/session_override.example.json` en `session_override.json`,
   mettre ton identifiant de compte Switch (le nom du dossier dans
   `tico/saves/users/`), ton pseudo et ton mot de passe RA, puis lancer
   `tools\get_ra_token.ps1`, qui remplace le mot de passe par un jeton.
3. Sur la SD : `session_override.json` dans `tico/`, et chaque `.nro` à la
   place de l'original dans `tico/modules/<émulateur>/` (sauvegarde les
   originaux avant).
4. PS2 : remplir pseudo et jeton dans `tico/system/nethersx2/nethersx2.ini`.
5. DS : expérimental, il faut ton propre APK DraStic acheté (voir plus haut).

Ne partage jamais ton `session_override.json` : il contient ton jeton RA.
Une mise à jour d'un émulateur depuis le tico Store remet la version
officielle : il faut alors recompiler.
