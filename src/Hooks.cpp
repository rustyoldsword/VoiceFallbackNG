#include "Hooks.h"

#include "Settings.h"
#include "VoiceLocation.h"
#include "Voices.h"

#include <MinHook.h>

namespace VoiceFallback::Hooks
{
	namespace
	{
		// ---- mode B: GetIsVoiceType ------------------------------------------------------------------------------
		// The condition is answered by a plain function pointer in the engine's command table, so it is swapped there.

		RE::SCRIPT_FUNCTION::Condition_t* originalGetIsVoiceType = nullptr;

		bool GetIsVoiceType(RE::TESObjectREFR* a_subject, void* a_param1, void* a_param2, double& a_result)
		{
			const bool ok = originalGetIsVoiceType(a_subject, a_param1, a_param2, a_result);
			const auto& settings = Settings::Get();
			if (ok && a_result == 0.0 && settings.bEnabled && settings.bUnlockLines && a_subject) {
				if (auto* actor = a_subject->As<RE::Actor>(); actor && Voices::Matches(actor, static_cast<RE::TESForm*>(a_param1))) {
					a_result = 1.0;
				}
			}
			return ok;
		}

		void InstallCondition()
		{
			auto* command = RE::SCRIPT_FUNCTION::LocateScriptCommand("GetIsVoiceType");
			if (!command || !command->conditionFunction) {
				logger::error("GetIsVoiceType not found in the command table, mode B is unavailable");
				return;
			}
			originalGetIsVoiceType = command->conditionFunction;
			command->conditionFunction = GetIsVoiceType;
			logger::info("GetIsVoiceType hooked");
		}

		// ---- audio: DialogueItem constructor ---------------------------------------------------------------------
		// The constructor turns an INFO into responses and writes each response's voice file path for the speaker's
		// voice type. After it ran, a path to a file nobody ships is pointed at the first fallback voice that has it.

		using DialogueCtor_t = RE::DialogueItem* (*)(RE::DialogueItem*, RE::TESQuest*, RE::TESTopic*, RE::TESTopicInfo*, RE::TESObjectREFR*);
		DialogueCtor_t originalCtor = nullptr;

		std::atomic<int>                      diagnostics{ 20 };

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// a_path is relative to Data and has no extension; the game plays .fuz, .xwm or .wav
		bool VoiceFileExists(const std::string& a_path)
		{
			return VoiceLocation::Exists(a_path);
		}

		struct VoicePath
		{
			std::string full;          // as the game wrote it
			std::size_t voiceBegin{};  // the voice type folder name inside full
			std::size_t voiceEnd{};
			std::size_t relBegin{};    // where "Sound\Voice\" starts
			std::size_t extBegin{};    // extension dot, or full.size()
		};

		std::optional<VoicePath> Parse(std::string_view a_path)
		{
			VoicePath  p;
			p.full = a_path;
			for (auto& c : p.full) {
				if (c == '/') {
					c = '\\';
				}
			}
			const auto lower = Lower(p.full);
			p.relBegin = lower.find("sound\\voice\\");
			if (p.relBegin == std::string::npos) {
				return std::nullopt;
			}
			const auto pluginEnd = lower.find('\\', p.relBegin + 12);
			if (pluginEnd == std::string::npos) {
				return std::nullopt;
			}
			p.voiceBegin = pluginEnd + 1;
			p.voiceEnd = lower.find('\\', p.voiceBegin);
			if (p.voiceEnd == std::string::npos) {
				return std::nullopt;
			}
			const auto dot = lower.rfind('.');
			p.extBegin = dot != std::string::npos && dot > p.voiceEnd ? dot : lower.size();
			return p;
		}

		std::string Relative(const VoicePath& a_path, std::string_view a_voice)
		{
			return a_path.full.substr(a_path.relBegin, a_path.voiceBegin - a_path.relBegin) + std::string(a_voice) +
			       a_path.full.substr(a_path.voiceEnd, a_path.extBegin - a_path.voiceEnd);
		}

		// The path the engine builds for a response before anything rewrites it (Fuz Ro D-oh swaps a missing file for
		// its own silent stock file inside the constructor):
		// Data\Sound\Voice\<plugin>\<voice>\<quest>_<topic>_<local id>_<n>.wav, quest cut to 10 and topic to 15
		// characters when both together are longer than 25.
		std::string EnginePath(const RE::DialogueItem* a_item, std::string_view a_voice, std::size_t a_number)
		{
			const auto* info = a_item->info;
			const auto* file = info ? info->GetFile(0) : nullptr;
			if (!file) {
				return {};
			}
			std::string quest = a_item->quest ? a_item->quest->formEditorID.c_str() : "";
			std::string topic = a_item->topic ? a_item->topic->formEditorID.c_str() : "";
			if (quest.size() + topic.size() > 25) {
				quest.resize(std::min<std::size_t>(quest.size(), 10));
				topic.resize(std::min<std::size_t>(topic.size(), 15));
			}
			const auto local = info->GetFormID() & (file->IsLight() ? 0xFFFu : 0xFFFFFFu);
			return std::format("Data\\Sound\\Voice\\{}\\{}\\{}_{}_{:08X}_{}.wav", file->GetFilename(), a_voice, quest, topic, local, a_number);
		}

		bool IsStandIn(std::string_view a_path)
		{
			return Lower(a_path).find("\\fuz ro doh\\") != std::string::npos;
		}

		void FixResponses(RE::DialogueItem* a_item, RE::TESObjectREFR* a_speaker)
		{
			if (!a_item || !Settings::Get().bEnabled) {
				return;
			}
			// the constructor argument is empty on some call paths; the item keeps its own speaker too
			auto* speaker = a_speaker ? a_speaker : a_item->speaker;
			auto* actor = speaker ? speaker->As<RE::Actor>() : nullptr;

			if (diagnostics > 0) {
				for (auto* response : a_item->responses) {
					if (response && diagnostics-- > 0) {
						logger::info("[diag] {:08X} voice path '{}'", a_item->info ? a_item->info->GetFormID() : 0, response->voice.c_str());
					}
				}
			}

			const bool log = Settings::Get().bLogSubstitutions;
			const auto infoID = a_item->info ? a_item->info->GetFormID() : 0;

			static thread_local std::vector<RE::BGSVoiceType*> candidates;
			auto* entry = Voices::Candidates(actor, candidates);
			if (!actor && log) {
				logger::info("{:08X} no actor speaker (argument {:08X}, item {:08X})", infoID, a_speaker ? a_speaker->GetFormID() : 0,
					a_item->speaker ? a_item->speaker->GetFormID() : 0);
			}
			if (!entry) {
				if (log && actor) {
					// a non-vanilla voice that is not handled is worth a line: it says why a speaker stays silent
					const auto* base = actor->GetActorBase();
					const auto* voice = base ? base->voiceType : nullptr;
					const auto* file = voice ? voice->GetFile(0) : nullptr;
					if (file && file->GetCompileIndex() > 4) {
						logger::info("{:08X} speaker {:08X} voice {} [{}]: not handled", infoID, actor->GetFormID(), voice->formEditorID.c_str(), file->GetFilename());
					}
				}
				return;
			}
			if (log) {
				logger::info("{:08X} speaker {:08X} voice {}: {} responses, {} fallback voices", infoID, actor->GetFormID(), entry->editorID, a_item->responses.size(), candidates.size());
			}
			std::size_t number = 0;
			for (auto* response : a_item->responses) {
				++number;
				if (!response) {
					continue;
				}
				std::string_view current = response->voice.empty() ? ""sv : std::string_view(response->voice.c_str());
				auto path = current.empty() || IsStandIn(current) ? std::nullopt : Parse(current);
				if (!path) {
					// no usable path (none, or Fuz Ro D-oh's silent stand-in): rebuild the one the engine looked for
					const auto rebuilt = EnginePath(a_item, entry->editorID, number);
					path = rebuilt.empty() ? std::nullopt : Parse(rebuilt);
					if (log) {
						logger::info("  voice path '{}' -> rebuilt '{}'", current, rebuilt);
					}
					if (!path) {
						continue;
					}
				}
				if (VoiceFileExists(Relative(*path, path->full.substr(path->voiceBegin, path->voiceEnd - path->voiceBegin)))) {
					if (path->full != current) {
						response->voice = path->full.c_str();  // a stand-in replaced a file that does exist
					}
					if (log) {
						logger::info("  own file {}", path->full);
					}
					continue;  // the unique voice has its own file
				}
				bool replaced = false;
				for (auto* voice : candidates) {
					const std::string_view name = voice->formEditorID.c_str();
					if (!VoiceFileExists(Relative(*path, name))) {
						continue;
					}
					const auto fixed = path->full.substr(0, path->voiceBegin) + std::string(name) + path->full.substr(path->voiceEnd);
					response->voice = fixed.c_str();
					++entry->swaps;
					replaced = true;
					if (Settings::Get().bLogSubstitutions) {
						logger::info("{} [{}]: {}", entry->editorID, name, fixed);
					}
					break;
				}
				if (!replaced) {
					++entry->misses;
					if (Settings::Get().bLogSubstitutions) {
						logger::info("{}: no voice has {}", entry->editorID, path->full);
					}
				}
			}
		}

		RE::DialogueItem* DialogueCtor(RE::DialogueItem* a_this, RE::TESQuest* a_quest, RE::TESTopic* a_topic, RE::TESTopicInfo* a_info, RE::TESObjectREFR* a_speaker)
		{
			auto* result = originalCtor(a_this, a_quest, a_topic, a_info, a_speaker);
			FixResponses(a_this, a_speaker);
			return result;
		}

		void InstallAudio()
		{
			const auto target = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(34413, 35220) }.address();
			if (const auto status = MH_Initialize(); status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
				logger::error("MinHook init failed, audio fallback is unavailable");
				return;
			}
			if (MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&DialogueCtor), reinterpret_cast<void**>(&originalCtor)) != MH_OK ||
				MH_EnableHook(reinterpret_cast<void*>(target)) != MH_OK) {
				logger::error("cannot hook the DialogueItem constructor at {:X}, audio fallback is unavailable", target);
				return;
			}
			logger::info("DialogueItem constructor hooked at {:X}", target - REL::Module::get().base());
		}

		// ---- audio: DialogueResponse constructor -----------------------------------------------------------------
		// Every response the game plays is built here, including the ones of the dialogue menu that never pass the
		// DialogueItem constructor; it writes the voice file path. Pointing that path at the fallback voice makes the
		// whole playback (audio, lip sync, Fuz Ro D-oh's existence check) use a file that really exists. The arguments
		// are not mapped, so all register and stack slots the caller may use are passed through untouched.
		using ResponseCtor_t = void* (*)(void*, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		ResponseCtor_t originalResponseCtor = nullptr;

		void* ResponseCtor(void* a_this, std::uintptr_t a2, std::uintptr_t a3, std::uintptr_t a4, std::uintptr_t a5, std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8)
		{
			VoiceLocation::ForgetLastRedirect();
			auto* result = originalResponseCtor(a_this, a2, a3, a4, a5, a6, a7, a8);
			auto* response = static_cast<RE::DialogueResponse*>(a_this);
			if (response && !response->voice.empty() && Settings::Get().bEnabled) {
				std::string original = response->voice.c_str();
				if (IsStandIn(original)) {
					// Fuz Ro D-oh replaced the path; the file the constructor asked for just before is the original
					auto asked = VoiceLocation::TakeLastRedirect();
					if (asked.empty()) {
						return result;  // a line no voice has: the silent stand-in is right
					}
					const auto dot = asked.rfind('.');
					original = "Data\\" + asked.substr(0, dot) + ".wav";
				}
				if (const auto fixed = VoiceLocation::FallbackVoicePath(original); !fixed.empty()) {
					if (Settings::Get().bLogSubstitutions) {
						logger::info("response {} -> {}", response->voice.c_str(), fixed);
					}
					response->voice = fixed.c_str();
				}
			}
			return result;
		}

		void InstallResponse()
		{
			if (!REL::Module::IsAE()) {
				logger::warn("DialogueResponse constructor is only mapped for AE, menu responses keep their own voice");
				return;
			}
			const auto target = REL::Relocation<std::uintptr_t>{ REL::ID(35249) }.address();
			if (MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&ResponseCtor), reinterpret_cast<void**>(&originalResponseCtor)) != MH_OK ||
				MH_EnableHook(reinterpret_cast<void*>(target)) != MH_OK) {
				logger::error("cannot hook the DialogueResponse constructor at {:X}", target);
				return;
			}
			logger::info("DialogueResponse constructor hooked at {:X}", target - REL::Module::get().base());
		}
	}

	void Install()
	{
		InstallCondition();
		InstallAudio();
		InstallResponse();
	}

	void ClearFileCache()
	{
		VoiceLocation::ClearCache();
	}
}
