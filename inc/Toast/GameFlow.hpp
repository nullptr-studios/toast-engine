/**
 * @file GameFlow.hpp
 * @author Dante Harper
 * @date 26/01/26
 */

#pragma once

#include <future>

namespace toast {
class GameFlow final {
	using level_id = unsigned;

	struct {
		std::vector<std::string> worldList;
		std::vector<std::string> levelList;

		std::optional<level_id> world;
		std::optional<level_id> level;

		std::optional<std::shared_future<level_id>> currentLevel;
		std::optional<std::shared_future<level_id>> nextLevel;
	} m;

public:
	GameFlow();

	void LoadWorld(unsigned world);
	void LoadLevel(unsigned world, unsigned level);

	void NextLevel();
	void NextWorld();
};
}
