#include "VoiceLocation.h"

#include "Settings.h"
#include "Voices.h"

namespace VoiceFallback::VoiceLocation
{
	namespace
	{
		using RE::BSResource::ErrorCode;

		// set while this plugin itself opens files, so its own location stays out of the answer
		thread_local bool bypass = false;

		struct Bypass
		{
			bool previous;
			Bypass() : previous(bypass) { bypass = true; }
			~Bypass() { bypass = previous; }
		};

		// the last missing voice file of a handled voice this thread asked for (relative path, as requested)
		thread_local std::string lastRedirected;

		std::mutex                                   cacheLock;
		std::unordered_map<std::string, bool>        existsCache;    // lower-case path -> exists in a real location
		std::unordered_map<std::string, std::string> redirectCache;  // lower-case requested path -> served path ("" = none)

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			for (auto& c : out) {
				c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return out;
		}

		bool RealFileExists(const std::string& a_path)
		{
			auto key = Lower(a_path);
			{
				std::scoped_lock guard(cacheLock);
				if (const auto it = existsCache.find(key); it != existsCache.end()) {
					return it->second;
				}
			}
			Bypass                       off;
			RE::BSResourceNiBinaryStream stream(a_path);
			const bool                   found = stream.good();
			std::scoped_lock             guard(cacheLock);
			existsCache.emplace(std::move(key), found);
			return found;
		}

		// "sound\voice\<plugin>\<voice>\<file>" -> path of the first fallback voice that has <file>, or ""
		std::string Redirect(std::string_view a_path)
		{
			const auto lower = Lower(a_path);
			const auto rel = lower.find("sound\\voice\\");
			if (rel == std::string::npos) {
				return {};
			}
			const auto pluginEnd = lower.find('\\', rel + 12);
			const auto voiceEnd = pluginEnd == std::string::npos ? std::string::npos : lower.find('\\', pluginEnd + 1);
			if (voiceEnd == std::string::npos) {
				return {};
			}
			{
				std::scoped_lock guard(cacheLock);
				if (const auto it = redirectCache.find(lower); it != redirectCache.end()) {
					return it->second;
				}
			}

			std::string                    served;
			std::vector<RE::BGSVoiceType*> candidates;
			const auto                     folder = std::string_view(lower).substr(pluginEnd + 1, voiceEnd - pluginEnd - 1);
			auto*                          entry = Voices::CandidatesForFolder(folder, candidates);
			const auto                     requested = std::string(a_path.substr(rel));
			// the game asks for .wav, .xwm and .fuz in turn; a voice that has the line in any of them keeps it
			const auto dot = requested.rfind('.');
			const auto stem = dot == std::string::npos ? requested : requested.substr(0, dot);
			const bool own = RealFileExists(stem + ".fuz") || RealFileExists(stem + ".xwm") || RealFileExists(stem + ".wav");
			if (entry && !own) {
				const auto head = requested.substr(0, pluginEnd + 1 - rel);
				const auto tail = requested.substr(voiceEnd - rel);
				for (auto* voice : candidates) {
					auto path = head + voice->formEditorID.c_str() + tail;
					if (RealFileExists(path)) {
						served = std::move(path);
						++entry->swaps;
						break;
					}
				}
				if (served.empty() && Lower(requested).ends_with(".fuz")) {
					++entry->misses;
				}
				const bool fuz = dot != std::string::npos && Lower(requested.substr(dot)) == ".fuz";
				if (Settings::Get().bLogSubstitutions && (!served.empty() || fuz)) {
					logger::info("file {} -> {}", requested, served.empty() ? "no fallback voice has it"s : served);
				}
			}
			std::scoped_lock guard(cacheLock);
			redirectCache.emplace(lower, served);
			return served;
		}

		// The game's location tree answers every file request by asking its child locations (loose files, archives)
		// in turn. Its DoCreateStream is swapped so that a missing voice file of a handled voice is looked up under the
		// fallback voice instead; the tree then opens it and reports the real owning location itself.
		using CreateStream_t = ErrorCode(RE::BSResource::LocationTree*, const char*, RE::BSTSmartPointer<RE::BSResource::Stream>&, RE::BSResource::Location*&, bool);
		CreateStream_t* originalCreateStream = nullptr;

		ErrorCode CreateStream(RE::BSResource::LocationTree* a_this, const char* a_path, RE::BSTSmartPointer<RE::BSResource::Stream>& a_stream, RE::BSResource::Location*& a_location, bool a_readOnly)
		{
			if (!bypass && a_path && Settings::Get().bEnabled) {
				if (const auto served = Redirect(a_path); !served.empty()) {
					lastRedirected = a_path;
					if (Settings::Get().bLogSubstitutions) {
						logger::info("  {} {}", "stream", served);
					}
					return originalCreateStream(a_this, served.c_str(), a_stream, a_location, a_readOnly);
				}
			}
			return originalCreateStream(a_this, a_path, a_stream, a_location, a_readOnly);
		}

		// voice playback streams the file asynchronously, a separate lookup
		using CreateAsyncStream_t = ErrorCode(RE::BSResource::LocationTree*, const char*, RE::BSTSmartPointer<RE::BSResource::AsyncStream>&, RE::BSResource::Location*&, bool);
		CreateAsyncStream_t* originalCreateAsyncStream = nullptr;

		ErrorCode CreateAsyncStream(RE::BSResource::LocationTree* a_this, const char* a_path, RE::BSTSmartPointer<RE::BSResource::AsyncStream>& a_stream, RE::BSResource::Location*& a_location, bool a_readOnly)
		{
			if (!bypass && a_path && Settings::Get().bEnabled) {
				if (const auto served = Redirect(a_path); !served.empty()) {
					lastRedirected = a_path;
					if (Settings::Get().bLogSubstitutions) {
						logger::info("  {} {}", "async", served);
					}
					return originalCreateAsyncStream(a_this, served.c_str(), a_stream, a_location, a_readOnly);
				}
			}
			return originalCreateAsyncStream(a_this, a_path, a_stream, a_location, a_readOnly);
		}

		// size and existence queries
		using GetInfo_t = ErrorCode(RE::BSResource::LocationTree*, const char*, RE::BSResource::Info&, RE::BSResource::Location*&);
		GetInfo_t* originalGetInfo = nullptr;

		ErrorCode GetInfo(RE::BSResource::LocationTree* a_this, const char* a_path, RE::BSResource::Info& a_info, RE::BSResource::Location*& a_location)
		{
			if (!bypass && a_path && Settings::Get().bEnabled) {
				if (const auto served = Redirect(a_path); !served.empty()) {
					lastRedirected = a_path;
					if (Settings::Get().bLogSubstitutions) {
						logger::info("  {} {}", "info", served);
					}
					return originalGetInfo(a_this, served.c_str(), a_info, a_location);
				}
			}
			return originalGetInfo(a_this, a_path, a_info, a_location);
		}
	}

	void Register()
	{
		if (originalCreateStream) {
			return;
		}
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSResource____LocationTree[0] };
		originalCreateStream = reinterpret_cast<CreateStream_t*>(vtable.write_vfunc(0x3, CreateStream));
		originalCreateAsyncStream = reinterpret_cast<CreateAsyncStream_t*>(vtable.write_vfunc(0x4, CreateAsyncStream));
		originalGetInfo = reinterpret_cast<GetInfo_t*>(vtable.write_vfunc(0x6, GetInfo));
		logger::info("voice file lookup hooked");
	}

	bool Exists(const std::string& a_path)
	{
		for (const auto ext : { ".fuz"sv, ".xwm"sv, ".wav"sv }) {
			if (RealFileExists(a_path + std::string(ext))) {
				return true;
			}
		}
		return false;
	}

	void ForgetLastRedirect()
	{
		lastRedirected.clear();
	}

	std::string TakeLastRedirect()
	{
		return std::exchange(lastRedirected, std::string{});
	}

	std::string FallbackVoicePath(std::string_view a_path)
	{
		// work on a copy with '\' separators; the stem is everything from "sound\voice\" up to the extension
		std::string path(a_path);
		std::ranges::replace(path, '/', '\\');
		const auto lower = Lower(path);
		const auto rel = lower.find("sound\\voice\\");
		if (rel == std::string::npos || lower.find("\\fuz ro doh\\") != std::string::npos) {
			return {};
		}
		const auto pluginEnd = lower.find('\\', rel + 12);
		const auto voiceEnd = pluginEnd == std::string::npos ? std::string::npos : lower.find('\\', pluginEnd + 1);
		if (voiceEnd == std::string::npos) {
			return {};
		}
		std::vector<RE::BGSVoiceType*> candidates;
		auto* entry = Voices::CandidatesForFolder(std::string_view(lower).substr(pluginEnd + 1, voiceEnd - pluginEnd - 1), candidates);
		if (!entry) {
			return {};
		}
		const auto dot = path.rfind('.');
		const auto ext = dot != std::string::npos && dot > voiceEnd ? path.substr(dot) : std::string{};
		const auto head = path.substr(0, pluginEnd + 1);                                // up to and including the plugin folder
		const auto file = path.substr(voiceEnd, path.size() - voiceEnd - ext.size());  // from the separator after the voice, no extension
		const auto anyFormat = [&](std::string_view a_voice) {
			const auto stem = head.substr(rel) + std::string(a_voice) + file;
			return RealFileExists(stem + ".fuz") || RealFileExists(stem + ".xwm") || RealFileExists(stem + ".wav");
		};
		if (anyFormat(entry->editorID)) {
			return {};  // the voice has the line itself
		}
		for (auto* voice : candidates) {
			const std::string_view name = voice->formEditorID.c_str();
			if (anyFormat(name)) {
				++entry->swaps;
				return head + std::string(name) + file + ext;
			}
		}
		++entry->misses;
		return {};
	}

	void ClearCache()
	{
		std::scoped_lock guard(cacheLock);
		existsCache.clear();
		redirectCache.clear();
	}
}
