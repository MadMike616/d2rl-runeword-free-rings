# Runeword-Free Rings

An offline Reign of the Warlock set for D2RLoader mods. The Unruned Oath has
six rings. The equip requirements are Dawn (40), Dusk (45),
Horizon (50), Zenith (55), Midnight (60), and Eclipse (65). All have drop
level 75. Each distinct pair has its own bonus, shown in the native set
bonus section when the formatter is available. Dawn + Dusk grants
+4 to All Skills and +20% each to Faster Cast Rate, Increased Attack Speed,
and Faster Run/Walk. The ring rolls and all 15 bonuses are defined in
`stage_tables.py`.

Any completed runeword on the character, including on the alternate weapon
set, disables the pair bonus. The individual ring stats remain active.
Runewords in inventory, stash, or on a mercenary do not count.

## Install

Download `d2rl-runeword-free-rings.dll` from the
[GitHub releases](https://github.com/MadMike616/d2rl-runeword-free-rings/releases).
You need a compatible D2RLoader installation and an unpacked, writable
Reign of the Warlock mod. In the standard layout the mod's loose data is
under `mods/<ModName>/<ModName>.mpq/data/`, where `<ModName>.mpq` is a
directory.

1. Check the required files listed below. Close the game and copy
   the release DLL to
   `mods/<ModName>/d2rloader/plugins/d2rl-runeword-free-rings.dll`.
2. Start D2RLoader with that mod selected. On a fresh install, the DLL
   merges the Oath rows and strings and backs up the files it changes to
   `mods/<ModName>/d2rloader/plugins/basefiles-and-backups/backups/runeword-free-rings/<timestamp>/`.
3. Wait for the first-run completion message in
   `mods/<ModName>/d2rloader/logs/runeword-free-rings.log`, then exit and
   restart D2RLoader with the same mod. Enter an offline Reign of the
   Warlock game.

The release DLL contains the installer and its data. The installer uses
the active mod's own table IDs and keeps its existing rows. The embedded
installation data in `skills.json` marks completion, so later launches
reuse the installed rows. Previously staged installations are picked up
on launch.

To upgrade an existing installation, close the game, replace the DLL in
the mod's plugin folder, and launch the same mod again. The DLL reuses
the installed ring data. If it finds the two old Oath spawn recipes in the
mod's loose `cubemain.txt`, it backs up the table and removes those rows.
If the log reports that it removed the recipes, restart D2RLoader to
refresh the game tables.

On first install, the DLL looks for each required file in the mod's loose
`<ModName>.mpq/data/` directory. When a file is missing there, it tries
`mods/<ModName>/d2rloader/plugins/basefiles-and-backups/basefiles/`.
Both folders use the paths below. Keep the mod's existing loose files
and reuse the originals already in `basefiles/`. Use Ladik's Casc Viewer
to extract any missing originals from the installed game's data.

| Directory under `data/` or `basefiles/` | Required files |
|---|---|
| `global/excel/` | `sets.txt`, `setitems.txt`, `skills.txt`, `states.txt`, `skilldesc.txt`, `properties.txt`, `itemstatcost.txt` |
| `local/lng/strings/` | `item-names.json`, `skills.json` |

The installer uses the extracted copies to create missing loose files
and keeps the originals in `basefiles/` untouched. If a required file is
missing from both folders, the log gives both full paths. Setup waits for
all inputs before editing any files. Supply the missing files and launch
again.

Setup holds the shared D2RLoader mod-install lock through backups and
file writes. Installers that use the same lock run one at a time.

To uninstall, first remove Oath rings from characters and stashes that will continue to use
the mod. With the game closed, remove the DLL and restore the affected
files from the original install backup, merging any later mod edits you want
to keep. Backups from versions through v0.0.15 are under
`mods/<ModName>/d2rloader/backups/runeword-free-rings/`. If the backup
contains `created-files.txt`, it lists loose files the installer created.
Remove those files or merge later edits you want to keep.

## Compatibility

Requires D2RLoader Plugin SDK 0.4.0 / ABI 4, game-thread callbacks, and the
validated native stat adapters. Uses ItemDetails for the runeword check when
available. On D2RLoader 1.3.1-beta it uses the verified native item flag.
The native adapters have been verified on D2R 3.4.95032 with D2RLoader
1.3.1-beta.

## Inspect and build from source

The source archive includes the C++ plugin and installer, the Windows
resource script, the embedded data generator and translations, the
D2RLoader Plugin SDK 0.4.0 headers, and the licenses and native hook
inventory. The reference directory contains the generated installer header
and linker map for the release DLL.

- `src/plugin.cpp` resolves the loaded tables, checks equipped items,
  applies the pair bonus, and formats item tooltips.
- `src/installer.cpp` finds the active mod, merges data, creates backups,
  and rolls back failed writes.
- `stage_tables.py` defines the ring rows and pair bonuses.
  `build-install-data.py` combines those definitions with
  `src/assets/bonus-labels.json` to generate `install-data.hpp`.
- `HOOK_INVENTORY.md` records the tooltip hook targets and their validation.

To build on Windows x64, install Python 3.10+, Visual Studio 2026 with
Desktop development with C++, and the Windows SDK. Open an **x64 Native
Tools Command Prompt for Visual Studio**, change to the plugin source
directory inside the extracted archive, and run:

```bat
build-release.cmd "..\build"
```

The script generates the installer header and builds the Release DLL and
linker map in the output directory. Keep the bundled `PluginSDK` folder
in the repository root or beside the plugin source directory in the release ZIP.

The v1.0.0 reference build used MSVC 19.51.36260, linker 14.51.36260.0,
Windows SDK 10.0.26100.0, and Python 3.13.14.
