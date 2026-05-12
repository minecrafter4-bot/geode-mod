#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <atomic>

using namespace geode::prelude;

class PvpSubmitter {
	int levelID = 0;
	int matchID = 0;
	float best = 0;
	std::atomic<bool> inPvp{ false };
	static async::TaskHolder<web::WebResponse> m_get_holder, m_put_holder;

	void submit();

public:
	PvpSubmitter();
	~PvpSubmitter();
	PvpSubmitter(int levelID);
	void record(float progress);
};

