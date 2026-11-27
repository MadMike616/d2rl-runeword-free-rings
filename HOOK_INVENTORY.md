# Hook Inventory — Runeword-Free Rings

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Runeword-Free Rings |
| Plugin ID | runeword-free-rings |
| Release version | 1.0.2 |
| Inventory updated | 2026-11-27 |
| Source revision | v1.0.2 release source |
| D2R build(s) audited | 3.4.95032 with D2RLoader 1.3.1-beta |
| D2RLoader SDK / Plugin ABI | 0.4.0 / 4 |
| Audit status | Complete |

## Scope and method

Inspected `src/plugin.cpp`, `src/installer.cpp`, `src/plugin.rc`, and the data generators.
The plugin uses SDK lifecycle and tooltip listeners and queues game-thread
work. Native helpers are called with checked byte witnesses. The tooltip
builder slots allow the Oath to format its text before display.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|

| H-01 | shared slot | Item tooltip text builder | RVA 0x3E2AF58 | Call through the previous slot target and format the returned Oath text | Native thunk and call-site bytes, executable target, atomic pointer swap, localized text match, equipped item identity, and checked native string assignment | src/plugin.cpp InstallNativeTooltip, BuildOathTooltip | Restores the previous target on unload when the slot is still owned by this plugin. |
| H-02 | shared slot | Equipped comparison tooltip text builder | RVA 0x3E2AF60 | Call through the previous slot target and format the returned Oath comparison text | Native thunk and call-site bytes, executable target, atomic pointer swap, localized text match, equipped slot, and checked native string assignment | src/plugin.cpp InstallNativeTooltip, BuildOathComparison | Restores the previous target on unload when the slot is still owned by this plugin. |

## Known collision overlaps

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|

| Perfect Frame | RVAs 0x3E2AF58 and 0x3E2AF60 | Same slots | `D2R-Tools-main/plugins/perfect-frame/src/plugin.cpp` swaps both slots. The Oath hooks call through the targets already installed by Perfect Frame. |

## Audit notes

Uses read-only lookup helpers at RVAs 0x48FE80, 0x48FF00 and 0x2F5940.
Uses D2RCore's checked stat list functions for the set's private aura.
Requires the aura's state and skill IDs to match before touching its list.
When ItemDetails is unavailable, checks D2RCore's Chronicle preview predicate
bytes before reading the completed-runeword flag from a server item resolved
through its SDK equipment handle.
Uses the validated player-to-client accessor and D2RCore state sender to
publish this aura's stat changes. The state-mask layout is checked against
the sender's instructions before marking this aura for serialization.
Uses the game's string assignment helper at RVA 0x80CF0 to preserve native
ownership if inserting the set bonuses requires more storage.
Version 0.0.10 keeps both hook sites and binds ring, skill, and state IDs
from the active mod's staged data. The state-mask stride uses the loaded
RotW States row count.
Version 0.0.11 adds the first-run data installer. Hook sites are unchanged.
Version 0.0.12 corrects the read-only SetItems row layout used during
startup. Hook sites are unchanged.
Version 0.0.13 adds a compiler-cache fallback for missing loose TXT
tables during first-run setup. Hook sites are unchanged.
Version 0.0.14 removes the spawn command and temporary diagnostics.
Both tooltip hook sites and their guards are unchanged.
Version 0.0.15 requires loose installer inputs and logs missing paths.
Hook sites and guards are unchanged.
Version 1.0.15 uses the shared plugins backup directory.
Hook sites and guards are unchanged.
Version 1.1.0 uses shared basefiles for missing installer inputs.
Hook sites and guards are unchanged.
The first public release, v1.0.0, preserves both hook sites and their guards
from the development build.
Version 1.0.1 validates and matches TXT table headers by name.
Hook sites and guards are unchanged.

Release 1.0.2 source review: TOML installation completion and configuration mirroring add no native hook, patch, vtable, or OS input-hook targets. Hook-site changes since the previous release: none. Startup completion was checked in the testing environment; the existing native validation limits above still apply.
