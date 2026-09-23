#include "PCH.h"

#include "Breath.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

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

	// Point de départ mémorisé pour la translation, par nom d'os : on déplace toujours
	// depuis la position d'origine de l'os, jamais depuis sa position déplacée précédente.
	std::unordered_map<const RE::NiAVObject*, RE::NiPoint3> g_restTranslation;

	// Liste dans le log tous les os du squelette dont le nom laisse penser qu'ils sont
	// autour du torse, pour aider à en choisir un sans deviner à l'aveugle.
	void ListTorsoBones(RE::NiAVObject* a_node, int a_depth = 0)
	{
		if (!a_node) {
			return;
		}

		static constexpr std::array<std::string_view, 9> keywords{
			"belly"sv, "stomach"sv, "torso"sv, "spine"sv, "chest"sv,
			"breast"sv, "com"sv, "pelvis"sv, "waist"sv
		};

		const std::string_view name = a_node->GetName();
		std::string lower{ name };
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		for (const auto& kw : keywords) {
			if (lower.find(kw) != std::string::npos) {
				logger::info("os disponible : \"{}\"", name);
				break;
			}
		}

		if (auto* asNode = a_node->IsNode()) {
			for (auto& child : asNode->children) {
				if (child) {
					ListTorsoBones(child.get(), a_depth + 1);
				}
			}
		}
	}

	// Exécuté sur le thread principal du jeu (via la file de tâches de F4SE).
	void Tick(const Respiration::Config& a_cfg)
	{
		static Respiration::BreathModel model;
		static auto last = std::chrono::steady_clock::now();
		static float debugTimer = 0.0f;
		static bool loggedFound = false;
		static bool listedBones = false;

		const auto now = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(now - last).count();
		last = now;

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		if (a_cfg.mode == Respiration::Mode::kListBones) {
			if (!listedBones) {
				listedBones = true;
				if (auto* root = player->Get3D(false)) {
					logger::info("--- os du torse détectés (3e personne) ---");
					ListTorsoBones(root);
					logger::info("--- fin de la liste : mets le nom voulu dans Bones= et remets Mode=Translate ---");
				}
			}
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

		const float wave = model.Step(a_cfg, dt, ratio);

		// On applique aux squelettes 3e personne et 1re personne (le nœud peut ne pas exister dans le second).
		std::size_t applied = 0;
		for (const bool firstPerson : { false, true }) {
			auto* root = player->Get3D(firstPerson);
			if (!root) {
				continue;
			}
			for (const auto& bone : a_cfg.bones) {
				const RE::BSFixedString name{ bone.c_str() };
				auto* node = root->GetObjectByName(name);
				if (!node) {
					continue;
				}
				++applied;

				if (a_cfg.mode == Respiration::Mode::kScale) {
					const float amp = model.Blend(a_cfg.restAmplitude, a_cfg.exhaustedAmplitude);
					node->local.scale = 1.0f + amp * wave;
				} else {
					auto [it, inserted] = g_restTranslation.try_emplace(node, node->local.translate);
					if (inserted) {
						continue;  // première frame vue : on mémorise juste la position de repos
					}
					const float amp = model.Blend(a_cfg.restTranslate, a_cfg.exhaustedTranslate);
					RE::NiPoint3 offset = it->second;
					const float delta = amp * wave;
					switch (a_cfg.translateAxis) {
					case 'x': offset.x += delta; break;
					case 'z': offset.z += delta; break;
					default: offset.y += delta; break;
					}
					node->local.translate = offset;
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
					"AP {:.1f}/{:.1f} ratio {:.2f} période {:.2f}s onde {:.3f} nœuds modifiés {}",
					current,
					maximum,
					ratio,
					model.Period(),
					wave,
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
