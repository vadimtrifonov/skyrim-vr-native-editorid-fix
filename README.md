# Native EditorID Fix VR

[CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) port of the original [Native EditorID Fix](https://www.nexusmods.com/skyrimspecialedition/mods/85260), built primarily for **Skyrim VR**.

`EditorID` is the Creation Kit record identifier, for example `GuardWhiterun` or `RecipeArmorIronDagger`. Skyrim does not preserve or expose those identifiers consistently at runtime; this plugin restores that behavior for runtime code and SKSE plugins.

The DLL remains `NativeEditorIDFix.dll`, so mods that depend on the original Native EditorID Fix API can use this as a drop-in replacement.

## Runtime support

| Runtime | Status | Recommendation |
| --- | --- | --- |
| Skyrim VR `1.4.15` | Primary target; extensively tested | Use this port |
| Skyrim SE `1.5.97` | Technically supported by the CommonLibSSE-NG build | Prefer the original [Native EditorID Fix](https://www.nexusmods.com/skyrimspecialedition/mods/85260) |
| Skyrim AE `1.6.1170` / GOG `1.6.1179` | Technically supported by the CommonLibSSE-NG build | Prefer the original [Native EditorID Fix](https://www.nexusmods.com/skyrimspecialedition/mods/85260) |

## Requirements

- Skyrim VR: [SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/30457) + [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
- Skyrim SE/AE: [SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/30379) + [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Features

Native EditorID Fix has two lookup directions: `EditorID -> form` and `form -> EditorID`. This port keeps both, plus the original safety and compatibility patches.

- **Native `EditorID -> form` lookup**
  - makes EditorIDs usable in engine lookup paths that normally require form IDs
  - enables bare EditorID resolution in console commands and SkyPatcher-style rules
- **Native `form -> EditorID` lookup**
  - lets Skyrim and SKSE plugins retrieve EditorIDs from loaded forms through native APIs
  - includes placed references, so in-world references and actors can expose EditorIDs too
- **Public `NEIF_*` API**
  - exports `NEIF_GetEditorIDLookupState` and `NEIF_GetEditorID`
  - preserves the original `NEIF::FormInfo` by-value call contract
- **Console `help` behavior**
  - lets `help` find records by EditorID
  - prefers full display names when present, with EditorID as fallback
- **Bounds-checked native formatting**
  - bounds selected unsafe engine string-formatting calls
  - reduces crash/corruption risk from oversized strings such as long EditorIDs
- **Node-naming compatibility**
  - keeps the original default compatibility mode where node names do not gain EditorIDs
  - can be changed in the INI when full native node naming is desired

## Configuration

```text
Data/SKSE/Plugins/NativeEditorIDFix.ini
```

Options are documented in the INI.

## API

Header:

```text
src/api/NativeEditorIDFixAPI.hpp
```

Exports:

```cpp
NEIF::EditorIDLookupState NEIF_GetEditorIDLookupState();
const char* NEIF_GetEditorID(NEIF::FormInfo form);
```

Lookup states:

| Value | Name | Meaning |
| --- | --- | --- |
| `0` | `None` | lookup disabled |
| `1` | `ExternalOnly` | direct API lookup only |
| `2` | `NativeNoNodes` | native lookup restored; node naming compatibility enabled |
| `3` | `Native` | native lookup restored, including node naming |

`NEIF_GetEditorID` always returns a non-null C string. Missing entries return `""`.

## Developing

From the repository root:

```powershell
git submodule update --init --recursive
.\scripts\Test.ps1 -Mode releasedbg
.\scripts\Package.ps1 -Mode releasedbg
```

The package script writes a mod-manager-ready archive under `artifacts/`.

Deploy directly to a mod root:

```powershell
.\scripts\Deploy.ps1 -Target "C:\Path\To\ModOrganizer\mods\Native EditorID Fix VR" -Mode releasedbg
```

`Deploy.ps1` creates `SKSE/Plugins/` under the target and copies the DLL and INI. Use `-OverwriteIni` to replace an existing INI.

## Credits

[Kitsuune](https://www.nexusmods.com/profile/KitsuuneNivis) - original author of [Native EditorID Fix](https://www.nexusmods.com/skyrimspecialedition/mods/85260)
