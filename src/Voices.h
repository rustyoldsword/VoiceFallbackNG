#pragma once

namespace VoiceFallback::Voices
{
	// One handled (unique) voice type.
	struct Entry
	{
		RE::BGSVoiceType*              voice{ nullptr };
		std::string                    editorID;
		std::string                    plugin;
		bool                           fromRule{ false };  // listed in a VoiceFallbackNG\*.json rule
		std::vector<RE::BGSVoiceType*> chain;              // rule chain; empty = the sex default from the ini
		bool                           hadDefaultDialogue{ false };
		bool                           person{ false };    // some NPC of a grown-up person race speaks with it
		std::vector<std::string>       characters;         // "Name (Plugin.esp)" of the mod NPCs that speak with it
		bool                           ruleOff{ false };   // a JSON rule says "false" for it
		std::string                    ruleFile;           // the JSON file that set its chain or switched it off
		std::atomic<bool>              enabled{ true };    // false: behaves exactly as without this mod
		std::atomic<std::uint32_t>     unlocks{ 0 };  // voice-type checks answered "yes" for it
		std::atomic<std::uint32_t>     swaps{ 0 };    // responses that borrowed another voice's file
		std::atomic<std::uint32_t>     misses{ 0 };   // responses no fallback voice had a file for
	};

	// Reads the ini chains and the JSON rules and decides which voice types are handled. Safe to call again.
	void Build();

	// Mode B sets "Allow Default Dialogue" on handled voices; switching it off restores each voice's own flag.
	void ApplyDefaultDialogueFlag();

	// GetIsVoiceType helper: true when the actor's voice is handled and a_form (voice type or form list) names one
	// of its fallback voices.
	bool Matches(RE::Actor* a_actor, const RE::TESForm* a_form);

	// Audio helper: the handled entry of the speaker's voice and the voices to try, best first.
	Entry* Candidates(RE::Actor* a_actor, std::vector<RE::BGSVoiceType*>& a_out);

	// File helper: the handled entry whose EditorID is the voice folder name in a voice file path, and the voices to
	// try for it. Without an actor the sex comes from the voice type itself; only voices of person NPCs (or ruled
	// ones) qualify, so creature voices never borrow human lines.
	Entry* CandidatesForFolder(std::string_view a_folder, std::vector<RE::BGSVoiceType*>& a_out);

	// Snapshot for the menu: the voices of mod characters (person NPCs) and ruled voices, sorted by character name.
	std::vector<Entry*> List();

	// Menu switch for one voice: stored in the ini, wins over the JSON rules; takes effect at once.
	void SetEnabled(Entry* a_entry, bool a_enabled);
}
