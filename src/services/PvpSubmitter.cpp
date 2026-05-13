#include "PvpSubmitter.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "AuthService.hpp"
#include "CheatGuard.hpp"
#include "../common.hpp"

async::TaskHolder<web::WebResponse> PvpSubmitter::m_get_holder, PvpSubmitter::m_put_holder;

PvpSubmitter::PvpSubmitter() {}

PvpSubmitter::~PvpSubmitter() {
	m_get_holder.cancel();
	m_put_holder.cancel();
}

PvpSubmitter::PvpSubmitter(int levelID): levelID(levelID) {
	if (!AuthService::isLoggedIn()) {
		return;
	}

	web::WebRequest req = web::WebRequest();
	std::string url = API_URL + "/levels/" + std::to_string(levelID) + "/inPvp";
	std::string APIKey = AuthService::getToken();

	req.header("Authorization", "Bearer " + APIKey);
	m_get_holder.spawn(req.get(url), [this](web::WebResponse res) {
		try {
			if (!res.ok()) {
				return;
			}

			auto json = res.json().unwrap();

			if (json["matchId"].isNumber()) {
				matchID = static_cast<int>(json["matchId"].asDouble().unwrap());
				inPvp.store(matchID > 0);
			}
		} catch (...) {
			log::warn("Failed to check active PvP match for level {}", this->levelID);
		}
	});
}

void PvpSubmitter::submit() {
	if (!inPvp.load() || matchID <= 0) {
		return;
	}

	if (!CheatGuard::canSubmitGameplayApi()) {
		return;
	}

	web::WebRequest req = web::WebRequest();
	std::string url = API_URL + "/pvp/matches/" + std::to_string(matchID) + "/progress?progress=" + std::to_string(best);
	std::string APIKey = AuthService::getToken();

	req.header("Authorization", "Bearer " + APIKey);
	m_put_holder.spawn(req.put(url), [](web::WebResponse res) {});
}

void PvpSubmitter::record(float progress) {
	if (progress <= best) {
		return;
	}

	best = progress;
	submit();
}
