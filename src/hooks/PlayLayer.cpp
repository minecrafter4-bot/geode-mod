#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/modify/PlayLayer.hpp> // DO NOT REMOVE
#include <Geode/binding/CheckpointGameObject.hpp>
#include <algorithm>
#include <string>
#include <unordered_set>
#include "../services/AttemptCounter.hpp"
#include "../services/DeathCounter.hpp"
#include "../services/EventSubmitter.hpp"
#include "../services/RaidSubmitter.hpp"
#include "../services/PvpSubmitter.hpp"
#include "../services/PvpOverlay.hpp"
#include "../services/AuthService.hpp"

using namespace geode::prelude;

class $modify(DTPlayLayer, PlayLayer) {
	struct Fields {
		bool hasRespawned = false;
		AttemptCounter attemptCounter;
		DeathCounter deathCounter;
		EventSubmitter *eventSubmitter;
		RaidSubmitter *raidSubmitter;
		PvpSubmitter *pvpSubmitter;
		PvpOverlay *pvpOverlay = nullptr;
		std::unordered_set<int> platformerCheckpointIds;
		int platformerCheckpointCount = 0;
	};

	static void onModify(auto& self) {
		(void)self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First);
	}

	bool isRunCheated() {
		return false; // Permanently bypassed
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

		if (AuthService::isLoggedIn() && !m_isPracticeMode) {
			m_fields->pvpOverlay = new PvpOverlay(this, id, m_fields->pvpSubmitter);
		}

		return true;
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);

		if (m_fields->pvpOverlay) {
			m_fields->pvpOverlay->update(dt);
		}
	}

	void destroyPlayer(PlayerObject * player, GameObject * p1) {
		PlayLayer::destroyPlayer(player, p1);

		if (m_level->isPlatformer() || m_isPracticeMode) {
			return;
		}

		if (!player->m_isDead) {
			return;
		}

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
		m_fields->pvpSubmitter->recordDeath(progress);
	}

	void levelComplete() {
		PlayLayer::levelComplete();

		if (!m_isPracticeMode) {
			if (m_level->isPlatformer()) {
				m_fields->pvpSubmitter->completePlatformer(m_fields->platformerCheckpointCount);
				return;
			}

			bool isCheated = isRunCheated();
			log::info("Run completed on level {}: {}", m_level->m_levelID.value(), isCheated ? "cheated" : "not cheated");

			if (isCheated) {
				return;
			}

			m_fields->eventSubmitter->record(100);
			m_fields->raidSubmitter->record(100);
			m_fields->pvpSubmitter->record(100);
			m_fields->pvpSubmitter->flushDeathCount();
			m_fields->deathCounter.setCompleted(true);
		}
	}

	void checkpointActivated(CheckpointGameObject* object) {
		PlayLayer::checkpointActivated(object);

		if (!object || !m_level->isPlatformer() || m_isPracticeMode) {
			return;
		}

		const int checkpointID = object->m_uniqueID > 0 ? object->m_uniqueID : object->m_objectID;
		if (checkpointID <= 0) {
			return;
		}

		if (m_fields->platformerCheckpointIds.insert(checkpointID).second) {
			m_fields->platformerCheckpointCount = static_cast<int>(m_fields->platformerCheckpointIds.size());
			m_fields->pvpSubmitter->recordCheckpoint(m_fields->platformerCheckpointCount);
		}
	}

	void resetLevel() {
		PlayLayer::resetLevel();

		if (!m_level->isPlatformer()) {
			m_fields->platformerCheckpointIds.clear();
			m_fields->platformerCheckpointCount = 0;
		}
		m_fields->hasRespawned = true;
	}

	void onQuit() {
		delete m_fields->pvpOverlay;
		m_fields->pvpOverlay = nullptr;

		bool isCheated = isRunCheated();

		PlayLayer::onQuit();

		if (isCheated) {
			log::info("Skipping gameplay API submissions because the run is cheated");
		} else if (!m_level->isPlatformer()) {
			m_fields->attemptCounter.submit();
			m_fields->deathCounter.submit();
		}
		if (!m_level->isPlatformer()) {
			m_fields->pvpSubmitter->flushDeathCount();
		}

		delete m_fields->eventSubmitter;
		delete m_fields->raidSubmitter;
		delete m_fields->pvpSubmitter;
	}
};
