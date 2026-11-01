#pragma once

namespace VoiceFallback
{
	struct Settings
	{
		bool        bEnabled{ true };           // master switch
		bool        bUnlockLines{ true };       // mode B: voice-type conditions treat a unique voice as its fallbacks
		bool        bAutoDetect{ true };        // every non-vanilla adult voice type is handled, not only the JSON rules
		bool        bLastResort{ true };        // audio may come from any vanilla adult voice of the speaker's sex
		bool        bLogSubstitutions{ true };  // one log line per borrowed audio file
		std::string sFemaleChain{ "FemaleNord,FemaleEvenToned,FemaleYoungEager" };
		std::string sMaleChain{ "MaleNord,MaleEvenToned,MaleYoungEager" };
		// menu choices per voice type: lower-case EditorID -> handled; wins over the JSON rules
		std::map<std::string, bool> voiceSwitches;

		static Settings& Get();
		static void      Load();
		static void      Save();
	};
}
