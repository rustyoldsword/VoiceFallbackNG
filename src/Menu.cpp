#include "Menu.h"

#include "Hooks.h"
#include "Settings.h"
#include "Voices.h"

#pragma warning(push)
#pragma warning(disable : 4099 5054)
#include <SKSEMenuFramework.h>
#pragma warning(pop)

namespace VoiceFallback::Menu
{
	namespace
	{
		void Help(const char* a_text)
		{
			if (a_text && ImGuiMCP::IsItemHovered()) {
				ImGuiMCP::SetTooltip("%s", a_text);
			}
		}

		bool Toggle(const char* a_label, bool& a_value, const char* a_help = nullptr)
		{
			const bool changed = ImGuiMCP::Checkbox(a_label, std::addressof(a_value));
			if (changed) {
				Settings::Save();
			}
			Help(a_help);
			return changed;
		}

		void Rebuild()
		{
			Voices::Build();
			Voices::ApplyDefaultDialogueFlag();
		}

		void __stdcall SettingsPage()
		{
			auto& s = Settings::Get();
			ImGuiMCP::SeparatorText("Behaviour");
			if (Toggle("Enabled", s.bEnabled, "Off: unique voices behave exactly as without this mod.")) {
				Voices::ApplyDefaultDialogueFlag();
			}
			if (Toggle("Unlock lines reserved for other voices", s.bUnlockLines,
					"On: checks for a voice type (GetIsVoiceType) accept a unique voice as its fallback voices, so the character "
					"also gets the lines other mods give only to those voices, spoken with their audio.\n"
					"Off: the character only says the lines it already gets; missing audio is still borrowed.")) {
				Voices::ApplyDefaultDialogueFlag();
			}
			if (Toggle("Handle every non-vanilla voice", s.bAutoDetect,
					"On: every adult voice type that does not come from Skyrim.esm or the official DLC is handled.\n"
					"Off: only the voice types listed in SKSE\\Plugins\\VoiceFallbackNG\\*.json.")) {
				Rebuild();
			}
			Toggle("Any vanilla voice as a last resort", s.bLastResort,
				"If no voice of the chain has the line, use the first vanilla adult voice of the same sex that has it.");
			Toggle("Log every borrowed line", s.bLogSubstitutions, "Writes VoiceFallbackNG.log in the SKSE log folder.");

			ImGuiMCP::SeparatorText("Fallback voices for voices without a rule");
			static char female[512]{};
			static char male[512]{};
			static bool loaded = false;
			if (!loaded) {
				strncpy_s(female, s.sFemaleChain.c_str(), _TRUNCATE);
				strncpy_s(male, s.sMaleChain.c_str(), _TRUNCATE);
				loaded = true;
			}
			ImGuiMCP::InputText("Female", female, sizeof(female));
			Help("Voice type EditorIDs, comma separated, best first.");
			ImGuiMCP::InputText("Male", male, sizeof(male));
			Help("Voice type EditorIDs, comma separated, best first.");
			if (ImGuiMCP::Button("Apply and rescan rules")) {
				s.sFemaleChain = female;
				s.sMaleChain = male;
				Settings::Save();
				Hooks::ClearFileCache();
				Rebuild();
			}
			Help("Also rereads the JSON rules and forgets which voice files exist.");
		}

		bool Contains(std::string_view a_text, std::string_view a_part)
		{
			return a_part.empty() || !std::ranges::search(a_text, a_part, [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			}).empty();
		}

		// One row per unique voice of mod characters. The switch acts on the voice, because the borrowed audio is chosen
		// by the voice folder of the file; a voice shared by several characters lists them all.
		void __stdcall CharactersPage()
		{
			const auto list = Voices::List();
			int        on = 0;
			for (const auto* entry : list) {
				on += entry->enabled ? 1 : 0;
			}
			ImGuiMCP::Text("%d characters' voices, %d handled", static_cast<int>(list.size()), on);
			ImGuiMCP::TextWrapped(
				"Unticked: the character keeps its own voice only and stays silent where it has no line, exactly as without this mod. "
				"The choice is saved in VoiceFallbackNG.ini and wins over the JSON rules.");

			static char filter[128]{};
			ImGuiMCP::InputText("Search", filter, sizeof(filter));
			Help("Part of a character name, voice type or plugin.");

			// text columns share the width and clip; a long list of names must not squeeze the others
			if (!ImGuiMCP::BeginTable("characters", 7,
					ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_SizingStretchProp |
						ImGuiMCP::ImGuiTableFlags_Resizable)) {
				return;
			}
			ImGuiMCP::TableSetupColumn("On", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed);
			ImGuiMCP::TableSetupColumn("Character", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 3.0f);
			ImGuiMCP::TableSetupColumn("Voice type", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 2.2f);
			ImGuiMCP::TableSetupColumn("Fallbacks", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGuiMCP::TableSetupColumn("Set by", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGuiMCP::TableSetupColumn("Borrowed", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed);
			ImGuiMCP::TableSetupColumn("Not found", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed);
			ImGuiMCP::TableHeadersRow();
			for (auto* entry : list) {
				std::string names;
				for (const auto& name : entry->characters) {
					names += (names.empty() ? "" : ", ") + name;
				}
				if (names.empty()) {
					names = "(no named character)";
				}
				if (!Contains(names, filter) && !Contains(entry->editorID, filter) && !Contains(entry->plugin, filter)) {
					continue;
				}
				std::string chain;
				for (auto* voice : entry->chain) {
					chain += (chain.empty() ? "" : ", ") + std::string(voice->formEditorID.c_str());
				}
				const bool  stored = Settings::Get().voiceSwitches.contains([&] {
                    auto id = entry->editorID;
                    std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    return id;
                }());
				std::string source = stored ? "menu" : entry->ruleFile.empty() ? "automatic" : entry->ruleFile;

				ImGuiMCP::TableNextRow();
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::PushID(entry->editorID.c_str());
				bool enabled = entry->enabled;
				if (ImGuiMCP::Checkbox("##on", std::addressof(enabled))) {
					Voices::SetEnabled(entry, enabled);
					Hooks::ClearFileCache();
				}
				ImGuiMCP::PopID();
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::TextUnformatted(names.c_str());
				Help(names.c_str());
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::TextUnformatted(entry->editorID.c_str());
				Help(entry->plugin.c_str());
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::TextUnformatted(entry->fromRule && !chain.empty() ? chain.c_str() : "default by sex");
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::TextUnformatted(source.c_str());
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::Text("%u", entry->swaps.load());
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::Text("%u", entry->misses.load());
			}
			ImGuiMCP::EndTable();
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not installed, no menu (the ini still works)");
			return;
		}
		SKSEMenuFramework::SetSection("Voice Fallback");
		SKSEMenuFramework::AddSectionItem("Settings", SettingsPage);
		SKSEMenuFramework::AddSectionItem("Characters", CharactersPage);
	}
}
