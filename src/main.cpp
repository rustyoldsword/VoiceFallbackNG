#include "Hooks.h"
#include "Menu.h"
#include "Settings.h"
#include "VoiceLocation.h"
#include "Voices.h"

namespace
{
	void InitializeLogging()
	{
		auto path = logger::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("Unable to lookup SKSE logs directory.");
		}
		*path /= "VoiceFallbackNG.log";

		auto log = std::make_shared<spdlog::logger>("Global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		using namespace VoiceFallback;
		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostPostLoad:
			Menu::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Voices::Build();
			Voices::ApplyDefaultDialogueFlag();
			VoiceLocation::Register();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitializeLogging();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} {} is loading", plugin->GetName(), plugin->GetVersion().string());

	SKSE::Init(a_skse);
	VoiceFallback::Settings::Load();
	VoiceFallback::Hooks::Install();

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		SKSE::stl::report_and_fail("Unable to register the SKSE message listener.");
	}

	logger::info("{} loaded", plugin->GetName());
	return true;
}
