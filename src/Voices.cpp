#include "Voices.h"

#include "Settings.h"

#include <nlohmann/json.hpp>

namespace VoiceFallback::Voices
{
	namespace
	{
		constexpr RE::FormID kActorTypeNPC = 0x13794;
		constexpr RE::FormID kActorTypeCreature = 0x13795;
		constexpr RE::FormID kActorTypeDaedra = 0x13797;
		constexpr RE::FormID kActorTypeAnimal = 0x13798;

		std::shared_mutex                                             lock;
		std::unordered_map<RE::BGSVoiceType*, std::unique_ptr<Entry>> entries;
		std::unordered_map<std::string, Entry*>                       byFolder;  // lower-case EditorID
		std::vector<RE::BGSVoiceType*>                                defaults[2];    // [male, female] ini chains
		std::vector<RE::BGSVoiceType*>                                lastResort[2];  // every vanilla adult voice

		bool IEquals(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
		}

		bool IContains(std::string_view a_text, std::string_view a_part)
		{
			return !std::ranges::search(a_text, a_part, [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			}).empty();
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		bool IsVanillaFile(const RE::TESFile* a_file)
		{
			if (!a_file) {
				return false;
			}
			const auto name = a_file->GetFilename();
			for (const auto vanilla : { "Skyrim.esm"sv, "Update.esm"sv, "Dawnguard.esm"sv, "HearthFires.esm"sv, "Dragonborn.esm"sv }) {
				if (IEquals(name, vanilla)) {
					return true;
				}
			}
			return false;
		}

		// Bethesda content: the base game, the DLC and Creation Club (cc*.esl / cc*.esm). Its voices are recorded by
		// Bethesda, so none of it is handled automatically nor listed as a mod character.
		bool IsOfficialFile(const RE::TESFile* a_file)
		{
			if (IsVanillaFile(a_file)) {
				return true;
			}
			if (!a_file) {
				return false;
			}
			const std::string_view name = a_file->GetFilename();
			return name.size() > 6 && _strnicmp(name.data(), "cc", 2) == 0 &&
			       (IEquals(name.substr(name.size() - 4), ".esl"sv) || IEquals(name.substr(name.size() - 4), ".esm"sv));
		}

		// Bethesda names creature voices "Cr" + capital (CrDragonVoice, ccASVSSE001_CrAshZombieVoice, BGSSSE003CrZombieVoice);
		// some creature races still carry ActorTypeNPC, so the race check alone lets them through.
		bool IsCreatureVoiceName(std::string_view a_id)
		{
			for (std::size_t i = 0; i + 2 < a_id.size(); ++i) {
				if (a_id[i] == 'C' && a_id[i + 1] == 'r' && std::isupper(static_cast<unsigned char>(a_id[i + 2])) &&
					(i == 0 || !std::isupper(static_cast<unsigned char>(a_id[i - 1])) || a_id[i - 1] == '_')) {
					return true;
				}
			}
			return false;
		}

		RE::BGSVoiceType* ByEditorID(std::string_view a_editorID)
		{
			for (auto* voice : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSVoiceType>()) {
				if (voice && IEquals(voice->formEditorID.c_str(), a_editorID)) {
					return voice;
				}
			}
			return nullptr;
		}

		std::vector<RE::BGSVoiceType*> ParseChain(const std::vector<std::string>& a_names, std::string_view a_where)
		{
			std::vector<RE::BGSVoiceType*> chain;
			for (auto name : a_names) {
				const auto first = name.find_first_not_of(" \t");
				const auto last = name.find_last_not_of(" \t");
				if (first == std::string::npos) {
					continue;
				}
				name = name.substr(first, last - first + 1);
				if (auto* voice = ByEditorID(name)) {
					chain.push_back(voice);
				} else {
					logger::warn("{}: voice type '{}' not found", a_where, name);
				}
			}
			return chain;
		}

		std::vector<std::string> SplitComma(std::string_view a_list)
		{
			std::vector<std::string> out;
			std::size_t              pos = 0;
			while (pos <= a_list.size()) {
				auto next = a_list.find(',', pos);
				if (next == std::string_view::npos) {
					next = a_list.size();
				}
				out.emplace_back(a_list.substr(pos, next - pos));
				pos = next + 1;
			}
			return out;
		}

		bool IsAdultVanillaVoice(const RE::BGSVoiceType* a_voice, bool a_female)
		{
			const std::string_view id = a_voice->formEditorID.c_str();
			const bool             female = a_voice->data.flags.any(RE::VOICE_TYPE_DATA::Flag::kFemale);
			const auto             prefix = a_female ? "Female"sv : "Male"sv;
			return female == a_female && id.size() > prefix.size() && _strnicmp(id.data(), prefix.data(), prefix.size()) == 0 &&
			       !IContains(id, "Child") && IsVanillaFile(a_voice->GetFile(0));
		}

		struct Rules
		{
			std::unordered_map<std::string, std::vector<std::string>> chains;    // lower-case EditorID -> chain (empty = sex default)
			std::unordered_set<std::string>                           off;       // lower-case EditorID: "false", listed but left alone
			std::unordered_map<std::string, std::string>              files;     // lower-case EditorID -> rule file name
			std::unordered_set<std::string>                           excluded;  // lower-case EditorID: never handled nor listed
		};

		Rules ReadRules()
		{
			Rules           rules;
			const auto      dir = std::filesystem::path("Data/SKSE/Plugins/VoiceFallbackNG");
			std::error_code ec;
			if (!std::filesystem::is_directory(dir, ec)) {
				return rules;
			}
			std::vector<std::filesystem::path> files;
			for (const auto& file : std::filesystem::directory_iterator(dir, ec)) {
				if (file.path().extension() == ".json") {
					files.push_back(file.path());
				}
			}
			std::ranges::sort(files);
			for (const auto& path : files) {
				try {
					std::ifstream in(path);
					const auto    json = nlohmann::json::parse(in, nullptr, true, true);
					if (const auto it = json.find("voiceTypes"); it != json.end() && it->is_object()) {
						// a voice maps to a list of fallback voices, to true (handled with the default chain), to false
						// (its characters keep their own silence, as without this mod), or to an object
						// { "fallbacks": [...], "enabled": false } - its own chain, used once the menu turns it on;
						// the last file read wins
						for (const auto& [id, value] : it->items()) {
							const auto key = Lower(id);
							rules.files[key] = path.filename().string();
							rules.off.erase(key);
							rules.chains.erase(key);
							if (value.is_boolean()) {
								if (value.get<bool>()) {
									rules.chains[key] = {};
								} else {
									rules.off.insert(key);
								}
							} else if (value.is_array()) {
								rules.chains[key] = value.get<std::vector<std::string>>();
							} else if (value.is_object()) {
								const auto fallbacks = value.find("fallbacks");
								rules.chains[key] = fallbacks != value.end() && fallbacks->is_array() ? fallbacks->get<std::vector<std::string>>() : std::vector<std::string>{};
								if (!value.value("enabled", true)) {
									rules.off.insert(key);
								}
							}
						}
					}
					if (const auto it = json.find("exclude"); it != json.end() && it->is_array()) {
						for (const auto& id : *it) {
							rules.excluded.insert(Lower(id.get<std::string>()));
						}
					}
					logger::info("rules: {}", path.filename().string());
				} catch (const std::exception& e) {
					logger::error("rules: {} is not valid ({})", path.filename().string(), e.what());
				}
			}
			return rules;
		}

		RE::BGSVoiceType* VoiceOf(RE::Actor* a_actor, bool& a_female, RE::TESRace*& a_race)
		{
			auto* base = a_actor->GetActorBase();
			if (!base) {
				return nullptr;
			}
			a_female = base->GetSex() == RE::SEX::kFemale;
			a_race = a_actor->GetRace();
			auto* voice = base->voiceType;  // the record field; the GetObjectVoiceType vfunc returns garbage on AE 1.6.1170
			if (!voice && a_race) {
				voice = a_race->defaultVoiceTypes[a_female ? 1 : 0];
			}
			return voice;
		}

		// Caller holds the lock.
		Entry* Find(RE::Actor* a_actor, bool& a_female)
		{
			if (!a_actor || entries.empty()) {
				return nullptr;
			}
			RE::TESRace* race = nullptr;
			auto*        voice = VoiceOf(a_actor, a_female, race);
			const auto   it = voice ? entries.find(voice) : entries.end();
			if (it == entries.end()) {
				return nullptr;
			}
			auto* entry = it->second.get();
			if (!entry->enabled) {
				return nullptr;
			}
			if (!entry->fromRule) {
				// automatic handling only for grown-up people other than the player: never creatures, daedra
				// (dremora with their own voices), animals or children
				if (!race || a_actor->IsPlayerRef() || race->data.flags.any(RE::RACE_DATA::Flag::kChild) || !race->HasKeywordID(kActorTypeNPC) ||
					race->HasKeywordID(kActorTypeCreature) || race->HasKeywordID(kActorTypeDaedra) || race->HasKeywordID(kActorTypeAnimal)) {
					return nullptr;
				}
			}
			return entry;
		}

		const std::vector<RE::BGSVoiceType*>& ChainOf(const Entry& a_entry, bool a_female)
		{
			return a_entry.chain.empty() ? defaults[a_female ? 1 : 0] : a_entry.chain;
		}
	}

	void Build()
	{
		const auto& settings = Settings::Get();
		auto*       data = RE::TESDataHandler::GetSingleton();
		const auto  rules = ReadRules();

		std::unique_lock guard(lock);
		// restore flags first, so a voice dropped from handling keeps its own flag
		for (auto& [voice, entry] : entries) {
			voice->data.flags.set(entry->hadDefaultDialogue, RE::VOICE_TYPE_DATA::Flag::kAllowDefaultDialogue);
		}
		entries.clear();
		byFolder.clear();

		defaults[0] = ParseChain(SplitComma(settings.sMaleChain), "sMaleChain");
		defaults[1] = ParseChain(SplitComma(settings.sFemaleChain), "sFemaleChain");
		lastResort[0].clear();
		lastResort[1].clear();

		for (auto* voice : data->GetFormArray<RE::BGSVoiceType>()) {
			if (!voice) {
				continue;
			}
			for (int sex = 0; sex < 2; ++sex) {
				if (IsAdultVanillaVoice(voice, sex == 1)) {
					lastResort[sex].push_back(voice);
				}
			}

			const std::string id = voice->formEditorID.c_str();
			const auto        key = Lower(id);
			const auto        rule = rules.chains.find(key);
			const bool        official = IsOfficialFile(voice->GetFile(0)) || IsCreatureVoiceName(id);
			if (rules.excluded.contains(key)) {
				continue;
			}
			const bool        ruleOff = rules.off.contains(key);
			if (rule == rules.chains.end() && !ruleOff && (!settings.bAutoDetect || official || id.empty() || IContains(id, "Child") || IContains(id, "Unvoiced"))) {
				continue;
			}

			auto entry = std::make_unique<Entry>();
			entry->voice = voice;
			entry->editorID = id;
			entry->plugin = voice->GetFile(0) ? std::string(voice->GetFile(0)->GetFilename()) : "?"s;
			entry->hadDefaultDialogue = voice->data.flags.any(RE::VOICE_TYPE_DATA::Flag::kAllowDefaultDialogue);
			if (rule != rules.chains.end()) {
				entry->fromRule = true;
				entry->chain = ParseChain(rule->second, id);
			}
			entry->ruleOff = ruleOff;
			if (const auto file = rules.files.find(key); file != rules.files.end()) {
				entry->ruleFile = file->second;
			}
			const auto choice = settings.voiceSwitches.find(key);
			entry->enabled = choice != settings.voiceSwitches.end() ? choice->second : !ruleOff;
			byFolder[key] = entry.get();
			entries.emplace(voice, std::move(entry));
		}

		for (auto* npc : data->GetFormArray<RE::TESNPC>()) {
			auto* race = npc ? npc->race : nullptr;
			const auto it = npc && npc->voiceType ? entries.find(npc->voiceType) : entries.end();
			if (it != entries.end() && race && !race->data.flags.any(RE::RACE_DATA::Flag::kChild) && race->HasKeywordID(kActorTypeNPC) &&
				!race->HasKeywordID(kActorTypeCreature) && !race->HasKeywordID(kActorTypeDaedra) && !race->HasKeywordID(kActorTypeAnimal)) {
				auto* entry = it->second.get();
				entry->person = true;
				// the menu names a voice by its characters; leveled templates and nameless NPCs say nothing useful
				const auto* file = npc->GetFile(0);
				const std::string name = npc->fullName.c_str();  // the TESFullName field; vfunc reads are not trusted on this runtime
				if (!name.empty() && file && !IsOfficialFile(file) && !IContains(name, "Dummy")) {
					auto label = std::format("{} ({})", name, file->GetFilename());
					if (std::ranges::find(entry->characters, label) == entry->characters.end()) {
						entry->characters.push_back(std::move(label));
					}
				}
			}
		}

		logger::info("handling {} voice types; last resort: {} female / {} male vanilla voices", entries.size(), lastResort[1].size(), lastResort[0].size());
		for (const auto& [voice, entry] : entries) {
			std::string chain;
			for (auto* v : entry->chain) {
				chain += std::string(v->formEditorID.c_str()) + " ";
			}
			logger::info("  {} [{}] {}{}{}{}", entry->editorID, entry->plugin, entry->fromRule ? "rule: " : "auto", chain, entry->person ? "" : " (no person NPC)",
				entry->enabled ? "" : " (off)");
		}
	}

	void ApplyDefaultDialogueFlag()
	{
		const auto&      settings = Settings::Get();
		const bool       unlock = settings.bEnabled && settings.bUnlockLines;
		std::shared_lock guard(lock);
		for (auto& [voice, entry] : entries) {
			voice->data.flags.set((unlock && entry->enabled) || entry->hadDefaultDialogue, RE::VOICE_TYPE_DATA::Flag::kAllowDefaultDialogue);
		}
	}

	bool Matches(RE::Actor* a_actor, const RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		std::shared_lock guard(lock);
		bool             female = false;
		auto*            entry = Find(a_actor, female);
		if (!entry) {
			return false;
		}
		const auto& chain = ChainOf(*entry, female);
		bool        hit = false;
		if (const auto* voice = a_form->As<RE::BGSVoiceType>()) {
			hit = std::ranges::find(chain, voice) != chain.end();
		} else if (const auto* list = a_form->As<RE::BGSListForm>()) {
			hit = std::ranges::any_of(chain, [&](auto* v) { return list->HasForm(v); });
		}
		if (hit) {
			++entry->unlocks;
		}
		return hit;
	}

	Entry* Candidates(RE::Actor* a_actor, std::vector<RE::BGSVoiceType*>& a_out)
	{
		a_out.clear();
		std::shared_lock guard(lock);
		bool             female = false;
		auto*            entry = Find(a_actor, female);
		if (!entry) {
			return nullptr;
		}
		const auto add = [&](RE::BGSVoiceType* v) {
			if (v != entry->voice && std::ranges::find(a_out, v) == a_out.end()) {
				a_out.push_back(v);
			}
		};
		std::ranges::for_each(ChainOf(*entry, female), add);
		if (Settings::Get().bLastResort) {
			std::ranges::for_each(lastResort[female ? 1 : 0], add);
		}
		return entry;
	}

	Entry* CandidatesForFolder(std::string_view a_folder, std::vector<RE::BGSVoiceType*>& a_out)
	{
		a_out.clear();
		std::shared_lock guard(lock);
		const auto       it = byFolder.find(Lower(std::string(a_folder)));
		if (it == byFolder.end() || !it->second->enabled || !(it->second->person || it->second->fromRule)) {
			return nullptr;
		}
		auto*      entry = it->second;
		const bool female = entry->voice->data.flags.any(RE::VOICE_TYPE_DATA::Flag::kFemale);
		const auto add = [&](RE::BGSVoiceType* v) {
			if (v != entry->voice && std::ranges::find(a_out, v) == a_out.end()) {
				a_out.push_back(v);
			}
		};
		std::ranges::for_each(ChainOf(*entry, female), add);
		if (Settings::Get().bLastResort) {
			std::ranges::for_each(lastResort[female ? 1 : 0], add);
		}
		return entry;
	}

	std::vector<Entry*> List()
	{
		std::shared_lock    guard(lock);
		std::vector<Entry*> out;
		for (const auto& [voice, entry] : entries) {
			if (!entry->characters.empty()) {  // only named mod characters; rules still apply to the rest
				out.push_back(entry.get());
			}
		}
		const auto label = [](const Entry* a_entry) {
			return Lower(a_entry->characters.empty() ? a_entry->editorID : a_entry->characters.front());
		};
		std::ranges::sort(out, [&](auto* a, auto* b) { return label(a) < label(b); });
		return out;
	}

	void SetEnabled(Entry* a_entry, bool a_enabled)
	{
		if (!a_entry) {
			return;
		}
		a_entry->enabled = a_enabled;
		auto& settings = Settings::Get();
		const auto key = Lower(a_entry->editorID);
		// a choice equal to what the rules give is not stored, so a later JSON change still applies
		if (a_enabled == !a_entry->ruleOff) {
			settings.voiceSwitches.erase(key);
		} else {
			settings.voiceSwitches[key] = a_enabled;
		}
		Settings::Save();
		ApplyDefaultDialogueFlag();
		logger::info("menu: {} {}", a_entry->editorID, a_enabled ? "handled" : "left alone");
	}
}
