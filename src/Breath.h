#pragma once

// Logique pure : aucun appel au jeu ici, pour pouvoir la tester hors du jeu.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <istream>
#include <string>
#include <vector>

namespace Respiration
{
	struct Config
	{
		bool enabled{ true };
		bool debug{ false };
		std::vector<std::string> bones{ "Belly_skin" };
		float restPeriod{ 4.0f };           // secondes par respiration, reposé
		float exhaustedPeriod{ 1.0f };      // secondes par respiration, à bout de souffle
		float restAmplitude{ 0.015f };      // variation d'échelle de l'os, reposé
		float exhaustedAmplitude{ 0.05f };  // variation d'échelle de l'os, à bout de souffle
		float smoothing{ 2.0f };            // vitesse de lissage (plus grand = plus réactif)
	};

	inline std::string Trim(const std::string& a_str)
	{
		std::size_t first = 0;
		std::size_t last = a_str.size();
		while (first < last && std::isspace(static_cast<unsigned char>(a_str[first]))) {
			++first;
		}
		while (last > first && std::isspace(static_cast<unsigned char>(a_str[last - 1]))) {
			--last;
		}
		return a_str.substr(first, last - first);
	}

	inline std::string Lower(std::string a_str)
	{
		std::transform(a_str.begin(), a_str.end(), a_str.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return a_str;
	}

	inline std::vector<std::string> SplitBones(const std::string& a_list)
	{
		std::vector<std::string> result;
		std::size_t start = 0;
		while (start <= a_list.size()) {
			auto end = a_list.find(',', start);
			if (end == std::string::npos) {
				end = a_list.size();
			}
			auto name = Trim(a_list.substr(start, end - start));
			if (!name.empty()) {
				result.push_back(std::move(name));
			}
			start = end + 1;
		}
		return result;
	}

	inline bool ParseBool(const std::string& a_value)
	{
		const auto v = Lower(a_value);
		return v == "1" || v == "true" || v == "yes" || v == "on";
	}

	// Format attendu : lignes "Clé = valeur", commentaires avec ';' ou '#', sections ignorées.
	inline Config ParseConfig(std::istream& a_in)
	{
		Config cfg;
		std::string line;
		while (std::getline(a_in, line)) {
			if (const auto comment = line.find_first_of(";#"); comment != std::string::npos) {
				line.erase(comment);
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = Lower(Trim(line.substr(0, eq)));
			const auto value = Trim(line.substr(eq + 1));
			if (value.empty()) {
				continue;
			}

			try {
				if (key == "enabled") {
					cfg.enabled = ParseBool(value);
				} else if (key == "debug") {
					cfg.debug = ParseBool(value);
				} else if (key == "bones") {
					if (auto bones = SplitBones(value); !bones.empty()) {
						cfg.bones = std::move(bones);
					}
				} else if (key == "restperiod") {
					cfg.restPeriod = std::stof(value);
				} else if (key == "exhaustedperiod") {
					cfg.exhaustedPeriod = std::stof(value);
				} else if (key == "restamplitude") {
					cfg.restAmplitude = std::stof(value);
				} else if (key == "exhaustedamplitude") {
					cfg.exhaustedAmplitude = std::stof(value);
				} else if (key == "smoothing") {
					cfg.smoothing = std::stof(value);
				}
			} catch (...) {
				// valeur illisible : on garde la valeur par défaut
			}
		}

		cfg.restPeriod = std::max(cfg.restPeriod, 0.3f);
		cfg.exhaustedPeriod = std::max(cfg.exhaustedPeriod, 0.3f);
		cfg.restAmplitude = std::clamp(cfg.restAmplitude, 0.0f, 0.5f);
		cfg.exhaustedAmplitude = std::clamp(cfg.exhaustedAmplitude, 0.0f, 0.5f);
		cfg.smoothing = std::max(cfg.smoothing, 0.1f);
		return cfg;
	}

	// Modèle de respiration : période et amplitude suivent l'essoufflement, avec lissage.
	class BreathModel
	{
	public:
		// a_staminaRatio : 1.0 = pleine endurance, 0.0 = vide. Retourne l'échelle à appliquer à l'os.
		float Step(const Config& a_cfg, float a_dt, float a_staminaRatio)
		{
			const float dt = std::clamp(a_dt, 0.0f, 0.25f);
			const float ratio = std::clamp(a_staminaRatio, 0.0f, 1.0f);
			const float effort = 1.0f - ratio;  // 0 = reposé, 1 = épuisé

			const float targetPeriod = a_cfg.restPeriod + (a_cfg.exhaustedPeriod - a_cfg.restPeriod) * effort;
			const float targetAmplitude = a_cfg.restAmplitude + (a_cfg.exhaustedAmplitude - a_cfg.restAmplitude) * effort;

			if (!_initialized) {
				_period = targetPeriod;
				_amplitude = targetAmplitude;
				_initialized = true;
			}

			const float blend = 1.0f - std::exp(-a_cfg.smoothing * dt);
			_period += (targetPeriod - _period) * blend;
			_amplitude += (targetAmplitude - _amplitude) * blend;

			_phase += dt / std::max(_period, 0.2f);
			_phase -= std::floor(_phase);

			constexpr float twoPi = 6.28318530718f;
			const float wave = 0.5f - 0.5f * std::cos(_phase * twoPi);  // 0..1
			return 1.0f + _amplitude * wave;
		}

		[[nodiscard]] float Period() const noexcept { return _period; }
		[[nodiscard]] float Amplitude() const noexcept { return _amplitude; }

	private:
		bool _initialized{ false };
		float _phase{ 0.0f };
		float _period{ 4.0f };
		float _amplitude{ 0.015f };
	};
}
