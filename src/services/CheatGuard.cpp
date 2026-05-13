#include "CheatGuard.hpp"

#include <array>
#include <Geode/Geode.hpp>
#include <legowiifun.cheat_api/include/cheatAPI.hpp>

bool CheatGuard::canSubmitGameplayApi() {
	static constexpr std::array rulesetsToCheck = {
		ROBTOP,
		DEMONLIST,
		GDDL,
		MODMAKEROPINION,
		AREDL,
		PEMONLIST,
	};

	for (auto ruleset : rulesetsToCheck) {
		if (cheatAPI::isCheating(ruleset)) {
			geode::log::debug("Skipping gameplay API submission because Cheat API reports active cheat");
			return false;
		}
	}

	return true;
}
