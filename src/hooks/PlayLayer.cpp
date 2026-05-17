#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/modify/PlayLayer.hpp> // DO NOT REMOVE
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <optional>
#include "../services/AttemptCounter.hpp"
#include "../services/DeathCounter.hpp"
#include "../services/EventSubmitter.hpp"
#include "../services/RaidSubmitter.hpp"
#include "../services/PvpSubmitter.hpp"
#include "../services/PvpOverlay.hpp"
#include "../services/CheatGuard.hpp"
#include "../services/AuthService.hpp"

using namespace geode::prelude;

class $modify(DTPlayLayer, PlayLayer) {
	struct Fields {
		bool hasRespawned = false;
		bool isCheatedRun = false;
		bool noclipDetected = false;
		bool speedhackDetected = false;
		GameObject* disabledCheatObject = nullptr;
		std::optional<std::chrono::steady_clock::time_point> speedhackCompare;
		std::deque<double> realTimeHistory;
		std::deque<double> gameTimeHistory;
		double rollingRealSum = 0;
		double rollingGameSum = 0;
		double currentTimeWarp = 1;
		AttemptCounter attemptCounter;
		DeathCounter deathCounter;
		EventSubmitter *eventSubmitter;
		RaidSubmitter *raidSubmitter;
		PvpSubmitter *pvpSubmitter;
		PvpOverlay *pvpOverlay = nullptr;
	};

	static void onModify(auto& self) {
		(void)self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First);
	}

	bool isDamageBypassActive() {
		return m_isIgnoreDamageEnabled || m_ignoreDamage;
	}

	bool isRunCheated() {
		if (!m_fields->isCheatedRun && CheatGuard::isGameplayCheated()) {
			m_fields->isCheatedRun = true;
		}

		if (!m_fields->isCheatedRun && isDamageBypassActive()) {
			log::warn("Damage bypass detected on level {}", m_level->m_levelID.value());
			m_fields->isCheatedRun = true;
		}

		return m_fields->isCheatedRun || m_fields->noclipDetected || m_fields->speedhackDetected;
	}

	void resetSpeedhackSampler() {
		m_fields->realTimeHistory.clear();
		m_fields->gameTimeHistory.clear();
		m_fields->rollingRealSum = 0;
		m_fields->rollingGameSum = 0;
		m_fields->speedhackCompare = std::nullopt;
	}

	void checkNoclip(PlayerObject* player, GameObject* hitObject) {
		if (!m_fields->disabledCheatObject) {
			m_fields->disabledCheatObject = hitObject;
		}

		if (
			!m_fields->noclipDetected &&
			m_fields->disabledCheatObject != hitObject &&
			!player->m_isDead &&
			!m_levelEndAnimationStarted
		) {
			log::warn("Noclip detected on level {}", m_level->m_levelID.value());
			m_fields->noclipDetected = true;
			m_fields->isCheatedRun = true;
		}
	}

	void checkDelta(float delta) {
		if (!m_player1 || m_player1->m_isDead || m_isPaused) {
			return;
		}

		auto now = std::chrono::steady_clock::now();

		if (!m_fields->speedhackCompare.has_value()) {
			m_fields->speedhackCompare = now;
			return;
		}

		std::chrono::duration<double> realElapsed = now - m_fields->speedhackCompare.value();
		m_fields->speedhackCompare = now;

		auto gameDt = static_cast<double>(delta);
		auto realDt = realElapsed.count();

		if (realDt > 0.2) {
			return;
		}

		m_fields->rollingRealSum += realDt;
		m_fields->rollingGameSum += gameDt;
		m_fields->realTimeHistory.push_back(realDt);
		m_fields->gameTimeHistory.push_back(gameDt);

		constexpr size_t maxSamples = 120;
		if (m_fields->realTimeHistory.size() > maxSamples) {
			m_fields->rollingRealSum -= m_fields->realTimeHistory.front();
			m_fields->rollingGameSum -= m_fields->gameTimeHistory.front();
			m_fields->realTimeHistory.pop_front();
			m_fields->gameTimeHistory.pop_front();
		}

		if (m_fields->realTimeHistory.size() >= 30 && m_fields->rollingRealSum != 0) {
			auto currentRatio = m_fields->rollingGameSum / m_fields->rollingRealSum;
			auto expectedRatio = m_fields->currentTimeWarp;

			if (std::abs(currentRatio - expectedRatio) > 0.05 && !m_fields->speedhackDetected) {
				log::warn("Speedhack detected on level {}", m_level->m_levelID.value());
				m_fields->speedhackDetected = true;
				m_fields->isCheatedRun = true;
			}
		}
	}

	bool init(GJGameLevel * level, bool p1, bool p2) {
		if (!PlayLayer::init(level, p1, p2)) {
			return false;
		}

		int id = level->m_levelID.value();
		auto best = level->m_normalPercent.value();

		m_fields->deathCounter = DeathCounter(id, best >= 100);
		m_fields->eventSubmitter = new EventSubmitter(id);
		m_fields->raidSubmitter = new RaidSubmitter(id);
		m_fields->pvpSubmitter = new PvpSubmitter(id);
		m_fields->isCheatedRun = CheatGuard::isGameplayCheated();

		if (AuthService::isLoggedIn() && !m_level->isPlatformer() && !m_isPracticeMode) {
			m_fields->pvpOverlay = new PvpOverlay(this, id);
		}

		return true;
	}

	void postUpdate(float dt) {
		checkDelta(dt);
		PlayLayer::postUpdate(dt);

		if (!m_fields->isCheatedRun && !m_level->isPlatformer() && !m_isPracticeMode && isRunCheated()) {
			log::info("Run marked as cheated on level {}", m_level->m_levelID.value());
		}

		if (m_fields->pvpOverlay) {
			m_fields->pvpOverlay->update(dt);
		}
	}

	void destroyPlayer(PlayerObject * player, GameObject * p1) {
		PlayLayer::destroyPlayer(player, p1);

		resetSpeedhackSampler();
		checkNoclip(player, p1);

		if (!player->m_isDead) {
			return;
		}

		if (!m_level->isPlatformer() && !m_isPracticeMode) {
			bool isCheated = isRunCheated();
			log::info("Run ended on level {} at {}%: {}", m_level->m_levelID.value(), this->getCurrentPercentInt(), isCheated ? "cheated" : "not cheated");

			if (isCheated) {
				return;
			}

		    const float progress = std::min(this->getCurrentPercent(), 99.99f);

			m_fields->attemptCounter.add();
			m_fields->deathCounter.add(progress);
			m_fields->eventSubmitter->record(progress);
			m_fields->raidSubmitter->record(progress);
			m_fields->pvpSubmitter->record(progress);
		}
	}

	void levelComplete() {
		PlayLayer::levelComplete();

		if (!m_isPracticeMode) {
			bool isCheated = isRunCheated();
			log::info("Run completed on level {}: {}", m_level->m_levelID.value(), isCheated ? "cheated" : "not cheated");

			if (isCheated) {
				return;
			}

			m_fields->eventSubmitter->record(100);
			m_fields->raidSubmitter->record(100);
			m_fields->pvpSubmitter->record(100);
		    m_fields->deathCounter.setCompleted(true);
		}
	}

	void resetLevel() {
		PlayLayer::resetLevel();

		resetSpeedhackSampler();
		m_fields->hasRespawned = true;
		m_fields->isCheatedRun = CheatGuard::isGameplayCheated();
		m_fields->noclipDetected = false;
		m_fields->speedhackDetected = false;
		m_fields->disabledCheatObject = nullptr;
	}

	void updateTimeWarp(float timeWarp) {
		this->GJBaseGameLayer::updateTimeWarp(timeWarp);
		m_fields->currentTimeWarp = timeWarp;
		resetSpeedhackSampler();
	}

	void onQuit() {
		delete m_fields->pvpOverlay;
		m_fields->pvpOverlay = nullptr;

		bool isCheated = isRunCheated();

		PlayLayer::onQuit();

		if (isCheated) {
			log::info("Skipping gameplay API submissions because the run is cheated");
		} else {
			m_fields->attemptCounter.submit();
			m_fields->deathCounter.submit();
		}

		delete m_fields->eventSubmitter;
		delete m_fields->raidSubmitter;
		delete m_fields->pvpSubmitter;
	}
};
