#pragma once

namespace VoiceFallback::VoiceLocation
{
	// Hooks the game's file lookup so that requests for missing voice files of handled voices get the same file of
	// the first fallback voice that has it. Every reader goes through it: dialogue playback, lip sync, menu responses,
	// and other plugins that test whether a voice file exists (Fuz Ro D-oh).
	void Register();

	// True when a_path (relative to Data, with or without extension) exists in a real location. Never answered by
	// this plugin's own location.
	bool Exists(const std::string& a_path);

	// A response voice path ("Data\Sound\Voice\<plugin>\<voice>\<file>.wav") pointed at the first fallback voice that
	// has the line in any format, or "" when the voice is not handled, has the line itself, or no fallback has it.
	std::string FallbackVoicePath(std::string_view a_path);

	// The last missing voice file of a handled voice that this thread asked for, as requested ("Sound\Voice\...").
	// Fuz Ro D-oh tests the file inside the response constructor and then writes its silent stand-in, so this is
	// how the original path is recovered after that constructor.
	void        ForgetLastRedirect();
	std::string TakeLastRedirect();

	void ClearCache();
}
