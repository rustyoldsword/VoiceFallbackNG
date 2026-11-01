#include "Settings.h"

namespace VoiceFallback
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/VoiceFallbackNG.ini"sv;

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		bool Bool(const std::string& a_value) { return a_value == "1" || _stricmp(a_value.c_str(), "true") == 0; }
	}

	Settings& Settings::Get()
	{
		static Settings settings;
		return settings;
	}

	void Settings::Load()
	{
		auto&         s = Get();
		std::ifstream in{ std::filesystem::path(kPath) };
		if (!in) {
			logger::info("{} not found, using defaults", kPath);
			return;
		}
		std::string line;
		std::string section;
		while (std::getline(in, line)) {
			if (line.starts_with('[')) {
				section = Trim(line);
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos || line.starts_with(';')) {
				continue;
			}
			const auto key = Trim(std::string_view(line).substr(0, eq));
			const auto value = Trim(std::string_view(line).substr(eq + 1));
			if (section == "[Voices]") {
				auto id = key;
				std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				s.voiceSwitches[id] = Bool(value);
			} else if (key == "bEnabled") {
				s.bEnabled = Bool(value);
			} else if (key == "bUnlockLines") {
				s.bUnlockLines = Bool(value);
			} else if (key == "bAutoDetect") {
				s.bAutoDetect = Bool(value);
			} else if (key == "bLastResort") {
				s.bLastResort = Bool(value);
			} else if (key == "bLogSubstitutions") {
				s.bLogSubstitutions = Bool(value);
			} else if (key == "sFemaleChain") {
				s.sFemaleChain = value;
			} else if (key == "sMaleChain") {
				s.sMaleChain = value;
			}
		}
		logger::info("settings: enabled={} unlockLines={} autoDetect={} lastResort={} female=[{}] male=[{}]",
			s.bEnabled, s.bUnlockLines, s.bAutoDetect, s.bLastResort, s.sFemaleChain, s.sMaleChain);
	}

	void Settings::Save()
	{
		const auto&   s = Get();
		std::ofstream out{ std::filesystem::path(kPath), std::ios::trunc };
		if (!out) {
			logger::warn("cannot write {}", kPath);
			return;
		}
		out << "[General]\n"
			<< "; 0 = the mod does nothing\n"
			<< "bEnabled=" << (s.bEnabled ? 1 : 0) << "\n"
			<< "; 1 = voice-type checks (GetIsVoiceType) accept a unique voice as its fallback voices, so the speaker gets\n"
			<< ";     the lines other mods reserve for those voices; 0 = only borrow audio for lines the speaker already gets\n"
			<< "bUnlockLines=" << (s.bUnlockLines ? 1 : 0) << "\n"
			<< "; 1 = every adult voice type that does not come from Skyrim.esm or the official DLC is handled\n"
			<< "bAutoDetect=" << (s.bAutoDetect ? 1 : 0) << "\n"
			<< "; 1 = if no voice of the chain has the file, any vanilla adult voice of the same sex may be used\n"
			<< "bLastResort=" << (s.bLastResort ? 1 : 0) << "\n"
			<< "bLogSubstitutions=" << (s.bLogSubstitutions ? 1 : 0) << "\n\n"
			<< "[Fallback]\n"
			<< "; voice types (EditorID) tried in order, for voices that have no rule in VoiceFallbackNG\*.json\n"
			<< "sFemaleChain=" << s.sFemaleChain << "\n"
			<< "sMaleChain=" << s.sMaleChain << "\n\n"
			<< "[Voices]\n"
			<< "; switches from the menu: voice type EditorID = 1 handled / 0 left alone; wins over the JSON rules\n";
		for (const auto& [id, on] : s.voiceSwitches) {
			out << id << "=" << (on ? 1 : 0) << "\n";
		}
	}
}
