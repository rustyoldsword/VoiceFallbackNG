# Voice Fallback NG

Source code of Voice Fallback NG, a Skyrim SE/AE SKSE plugin for characters with a unique voice type (custom
followers, imported characters). When such a character says a line that has no voice file for her voice type, the
line plays with the first vanilla voice of her fallback chain that has the file; voice-type conditions
(`GetIsVoiceType`) can also accept her as those fallback voices, so she gets the lines other mods reserve for them.
No plugin file, no Papyrus.

The released mod (DLL, ini, rules, installer) is distributed separately; this repository holds the code that
builds it.

## How it works

- `Voices` finds the handled voice types (non-vanilla adult voices used by persons, plus the JSON rules in
  `Data/SKSE/Plugins/VoiceFallbackNG/*.json`) and their fallback chains, and keeps the per-voice switches.
- `Hooks` hooks the `DialogueItem` constructor (lines spoken outside dialogue menus) and the `DialogueResponse`
  constructor (dialogue menu lines) and points each response whose voice file is missing at a fallback voice.
  It also replaces the `GetIsVoiceType` condition function.
- `VoiceLocation` answers "does this voice file exist" (loose or in a BSA) and hooks the resource location tree so
  a missing voice file is opened from the fallback voice folder. This keeps it working next to Fuz Ro D-oh, which
  swaps missing voice paths for its silent stand-ins.
- `Menu` builds the SKSE Menu Framework pages (Settings, Characters); `Settings` reads and writes
  `VoiceFallbackNG.ini` (the defaults are in `data/`).

## Building

Requires [xmake](https://xmake.io) 3.x and Visual Studio 2022, plus two libraries placed in `lib/` (not included):

- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng) (alandtse's `ng` branch) at
  `lib/CommonLibSSE-NG`
- [SKSE-Menu-Framework-3-API](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API) at
  `lib/SKSE-Menu-Framework-3-API`

```
xmake f -m releasedbg --skyrim_vr=n
xmake build VoiceFallbackNG
```

Before building, apply `commonlib-unk8c.patch` inside `lib/CommonLibSSE-NG` (`git apply ../../commonlib-unk8c.patch`).
Older revisions of the `ng` branch declare an extra `TESObjectREFR` virtual that Skyrim SE/AE do not have, so in an
SE/AE-only build every later virtual is called one slot too far.

`data/` holds the default `VoiceFallbackNG.ini` and the rule files that ship next to the DLL.

## Credits

- Thiago099 and contributors - SKSE Menu Framework.
- CommonLibSSE-NG and its contributors; MinHook; nlohmann/json; the SKSE team.

## License

MIT, see `LICENSE`.
