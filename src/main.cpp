#include "PCH.h"

#include "Breath.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
	namespace fs = std::filesystem;
	using namespace std::chrono_literals;

	constexpr auto INI_PATH = "Data/F4SE/Plugins/Respiration.ini"sv;

	std::mutex g_configLock;
	std::shared_ptr<const Respiration::Config> g_config = std::make_shared<const Respiration::Config>();
	std::atomic_bool g_taskPending{ false };

	std::shared_ptr<const Respiration::Config> GetConfig()
	{
		std::lock_guard lock{ g_configLock };
		return g_config;
	}

	void ReloadConfig()
	{
		std::ifstream file{ fs::path{ INI_PATH } };
		if (!file) {
			logger::warn("{} introuvable, valeurs par défaut utilisées", INI_PATH);
			return;
		}

		auto cfg = std::make_shared<const Respiration::Config>(Respiration::ParseConfig(file));
		logger::info(
			"config chargée : enabled={} debug={} bones={} période {:.1f}s→{:.1f}s amplitude {:.3f}→{:.3f}",
			cfg->enabled,
			cfg->debug,
			cfg->bones.size(),
			cfg->restPeriod,
			cfg->exhaustedPeriod,
			cfg->restAmplitude,
			cfg->exhaustedAmplitude);

		std::lock_guard lock{ g_configLock };
		g_config = std::move(cfg);
	}

	// Exécuté sur le thread principal du jeu (via la file de tâches de F4SE).
	void Tick(const Respiration::Config& a_cfg)
	{
		static Respiration::BreathModel model;
		static auto last = std::chrono::steady_clock::now();
		static float debugTimer = 0.0f;
		static bool loggedFound = false;

		const auto now = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(now - last).count();
		last = now;

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto* avs = RE::ActorValue::GetSingleton();
		if (!avs || !avs->actionPoints) {
			return;
		}

		// L'endurance de Fallout 4 = les points d'action (AP), consommés par le sprint et le V.A.T.S.
		auto* owner = static_cast<RE::ActorValueOwner*>(player);
		const float current = owner->GetActorValue(*avs->actionPoints);
		const float maximum = owner->GetPermanentActorValue(*avs->actionPoints);
		const float ratio = maximum > 1.0f ? std::clamp(current / maximum, 0.0f, 1.0f) : 1.0f;

		const float scale = model.Step(a_cfg, dt, ratio);

		// On applique aux squelettes 3e personne et 1re personne (le nœud peut ne pas exister dans le second).
		std::size_t applied = 0;
		for (const bool firstPerson : { false, true }) {
			auto* root = player->Get3D(firstPerson);
			if (!root) {
				continue;
			}
			for (const auto& bone : a_cfg.bones) {
				const RE::BSFixedString name{ bone.c_str() };
				if (auto* node = root->GetObjectByName(name)) {
					node->local.scale = scale;
					++applied;
				}
			}
		}

		if (applied > 0 && !loggedFound) {
			loggedFound = true;
			logger::info("os trouvé(s), la respiration est active");
		}

		if (a_cfg.debug) {
			debugTimer += dt;
			if (debugTimer >= 2.0f) {
				debugTimer = 0.0f;
				logger::info(
					"AP {:.1f}/{:.1f} ratio {:.2f} période {:.2f}s échelle {:.4f} nœuds modifiés {}",
					current,
					maximum,
					ratio,
					model.Period(),
					scale,
					applied);
			}
		}
	}

	void WorkerLoop()
	{
		const auto* tasks = F4SE::GetTaskInterface();

		std::error_code ec;
		auto lastWrite = fs::last_write_time(fs::path{ INI_PATH }, ec);
		auto lastCheck = std::chrono::steady_clock::now();

		while (true) {
			std::this_thread::sleep_for(16ms);

			// rechargement à chaud de l'.ini (vérifié toutes les secondes)
			const auto now = std::chrono::steady_clock::now();
			if (now - lastCheck >= 1s) {
				lastCheck = now;
				std::error_code ecw;
				const auto stamp = fs::last_write_time(fs::path{ INI_PATH }, ecw);
				if (!ecw && stamp != lastWrite) {
					lastWrite = stamp;
					ReloadConfig();
				}
			}

			const auto cfg = GetConfig();
			if (!cfg->enabled) {
				continue;
			}

			// une seule tâche en attente à la fois, pour ne pas accumuler pendant les écrans de chargement
			if (g_taskPending.exchange(true)) {
				continue;
			}

			tasks->AddTask([cfg]() {
				try {
					Tick(*cfg);
				} catch (...) {
				}
				g_taskPending.store(false);
			});
		}
	}

	void F4SEAPI OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
			static std::once_flag once;
			std::call_once(once, [] {
				std::thread{ WorkerLoop }.detach();
				logger::info("boucle de respiration démarrée");
			});
		}
	}
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info("{} v{}", Version::PROJECT, Version::NAME);

	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_f4se->IsEditor()) {
		logger::critical("chargé dans l'éditeur");
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if (ver < F4SE::RUNTIME_1_10_163) {
		logger::critical("version du jeu non supportée : v{}", ver.string());
		return false;
	}

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	ReloadConfig();

	const auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		logger::critical("impossible de s'abonner aux messages F4SE");
		return false;
	}

	logger::info("plugin chargé");
	return true;
}
