// Test hors-jeu de Breath.h :  g++ -std=c++20 -I../src test_breath.cpp -o test_breath && ./test_breath

#include "Breath.h"

#include <cassert>
#include <cstdio>
#include <sstream>

using namespace Respiration;

static int CountBreaths(float a_ratio, float a_seconds)
{
	Config cfg;
	BreathModel model;
	int breaths = 0;
	float previous = model.Step(cfg, 0.016f, a_ratio);
	bool rising = false;
	// on laisse d'abord le lissage se stabiliser
	for (float t = 0; t < 10.0f; t += 0.016f) {
		previous = model.Step(cfg, 0.016f, a_ratio);
	}
	for (float t = 0; t < a_seconds; t += 0.016f) {
		const float value = model.Step(cfg, 0.016f, a_ratio);
		if (value > previous) {
			rising = true;
		} else if (value < previous && rising) {
			++breaths;  // un sommet franchi = une respiration
			rising = false;
		}
		previous = value;
	}
	return breaths;
}

int main()
{
	// 1. Config par défaut
	{
		std::istringstream in("");
		const auto cfg = ParseConfig(in);
		assert(cfg.enabled);
		assert(cfg.bones.size() == 1 && cfg.bones[0] == "Belly_skin");
		assert(cfg.restPeriod == 4.0f);
	}

	// 2. Config personnalisée, commentaires, casse, plusieurs os, valeur invalide
	{
		std::istringstream in(
			"[General]\n"
			"Enabled = 0   ; désactivé\n"
			"# commentaire\n"
			"Bones = Belly_skin, Chest ,  SPINE2\n"
			"RestPeriod=5.5\n"
			"ExhaustedPeriod=abc\n"
			"exhaustedamplitude = 0.08\n"
			"Debug=true\n");
		const auto cfg = ParseConfig(in);
		assert(!cfg.enabled);
		assert(cfg.debug);
		assert(cfg.bones.size() == 3);
		assert(cfg.bones[1] == "Chest" && cfg.bones[2] == "SPINE2");
		assert(cfg.restPeriod == 5.5f);
		assert(cfg.exhaustedPeriod == 1.0f);  // valeur invalide -> défaut
		assert(cfg.exhaustedAmplitude > 0.079f && cfg.exhaustedAmplitude < 0.081f);
	}

	// 3. Bornes de sécurité
	{
		std::istringstream in("RestPeriod=0\nRestAmplitude=9\nSmoothing=-3\n");
		const auto cfg = ParseConfig(in);
		assert(cfg.restPeriod >= 0.3f);
		assert(cfg.restAmplitude <= 0.5f);
		assert(cfg.smoothing >= 0.1f);
	}

	// 4. L'échelle reste dans [1, 1 + amplitude max]
	{
		Config cfg;
		BreathModel model;
		for (int i = 0; i < 5000; ++i) {
			const float ratio = (i / 500) % 2 ? 0.0f : 1.0f;
			const float s = model.Step(cfg, 0.016f, ratio);
			assert(s >= 1.0f - 1e-5f);
			assert(s <= 1.0f + cfg.exhaustedAmplitude + 1e-5f);
		}
	}

	// 5. Reposé ~ 15 respirations/min (période 4 s), épuisé ~ 60/min (période 1 s)
	{
		const int rested = CountBreaths(1.0f, 60.0f);
		const int exhausted = CountBreaths(0.0f, 60.0f);
		std::printf("reposé: %d resp/min, épuisé: %d resp/min\n", rested, exhausted);
		assert(rested >= 14 && rested <= 16);
		assert(exhausted >= 58 && exhausted <= 62);
	}

	// 6. Pas de saut brutal de période quand l'endurance s'effondre
	{
		Config cfg;
		BreathModel model;
		for (int i = 0; i < 1000; ++i) model.Step(cfg, 0.016f, 1.0f);
		model.Step(cfg, 0.016f, 0.0f);
		assert(model.Period() > 3.5f);  // encore proche de 4 s juste après la chute
	}

	std::puts("OK");
	return 0;
}
