# Voice Fallback NG 1.1.0

SKSE plugin for characters with a unique voice type (custom followers, imported characters).

Skyrim picks a voice file by the speaker's voice type and never falls back to another voice. A follower with
her own voice is therefore silent in every mod that was not voiced for her, and many mods hide such lines
completely with voice-type conditions, because unvoiced lines look broken. This plugin fixes both.

## What it does

1. **Borrowed audio.** When a handled character says a line and no file exists for her voice type, the
   response is pointed at the first fallback voice that has the file (loose or in a BSA). Her own files
   always win. Lip sync comes from the same .fuz.
2. **Unlocked lines** (on by default, switch in the menu). Voice-type checks (`GetIsVoiceType`, with a voice
   type or a form list) answer "yes" when the condition names one of her fallback voices, so she gets the
   lines other mods give to, say, FemaleNord. Her voice types also get "Allow Default Dialogue". With this
   switch off she only says the lines she already got, with borrowed audio.

## Which voices are handled

- Every adult voice type that does not come from Skyrim.esm, Update.esm, the official DLC or Creation Club
  (cc*.esl / cc*.esm), and is not a creature voice (Bethesda names those "Cr..."), (switch
  "Handle every non-vanilla voice"), but only for characters whose race is a person (ActorTypeNPC) and not
  a child race.
- Plus every voice type listed in `SKSE\Plugins\VoiceFallbackNG\*.json` (see `_example.json`). A character
  mod can ship its own file there. For each voice type the value is:
  - a list of voice types: its own fallback chain, best first;
  - `true`: handled with the default chain;
  - `false`: not handled. The character keeps its own voice and its silent lines, exactly as without this
    mod; use it for a voice so different that no vanilla voice fits. It still shows in the menu.
  - `{ "fallbacks": [ ... ], "enabled": false }`: its own chain, but off until turned on in the menu (opt-in);
    `"enabled"` may be left out and then means `true`.
- Listed under `exclude`, a voice type is never handled and not shown in the menu (creature voices).

Fallback chain without a rule: `sFemaleChain` / `sMaleChain` in `SKSE\Plugins\VoiceFallbackNG.ini`
(default FemaleNord, FemaleEvenToned, FemaleYoungEager and the male equivalents), chosen by the
character's sex. With "Any vanilla voice as a last resort", every vanilla adult voice of her sex is tried
after the chain.

## Menu

SKSE Menu Framework → Voice Fallback:

- **Settings**: the global switches and the default fallback chains.
- **Characters**: every mod character with a unique voice, with a search box and a tick per voice. Unticked,
  that voice is left alone as without this mod. The switch acts on the voice type, because the borrowed
  audio is chosen by the voice folder of the file; a voice shared by several characters lists them all. The
  choice is saved in `VoiceFallbackNG.ini` (`[Voices]`) and wins over the JSON rules; "Set by" shows where
  the current state comes from. The table also counts borrowed and missing lines.

Without SKSE Menu Framework the ini and the JSON rules work the same.

## Log

`Documents\My Games\Skyrim Special Edition\SKSE\VoiceFallbackNG.log`: the handled voice types after
loading and, with logging on, one line per borrowed or missing voice file.

## Requirements

SKSE, Address Library for SKSE Plugins. Optional: SKSE Menu Framework.
