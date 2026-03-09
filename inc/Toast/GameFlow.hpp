/**
 * @file GameFlow.hpp
 * @author Dante Harper
 * @date 26/01/26
 */

#pragma once

#include "Toast/Event/ListenerSubNode.hpp"

#include <future>

namespace toast {

class GameFlow final {
	event::ListenerSubNode listener;

	struct {
		std::vector<std::string> worldList;
		std::vector<std::string> levelList;

		std::optional<unsigned> world;
		std::optional<unsigned> level;

		std::optional<std::shared_future<unsigned>> currentLevel;
		std::optional<std::shared_future<unsigned>> nextLevel;

	} m;

	void LoadWorld(unsigned world);
	void LoadLevel(unsigned world, unsigned level);

	void NextLevel();
	void NextWorld();

	void Restart();

	static RootNode* currentRootNode;

public:
	GameFlow();

	static RootNode* CurrentRootNode();
};

}
