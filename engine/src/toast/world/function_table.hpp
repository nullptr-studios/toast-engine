/**
 * @file function_table.hpp
 * @author Xein
 * @date 18 May 2026
 *
 * @brief Struct that contains pointers to all of the tick functions of a node
 */

#pragma once
#include <functional>
#include <toast/export.hpp>
#include <vector>

namespace toast {
class Node;

struct TOAST_API NodeFunctionTable {
	struct {
		std::vector<std::function<void(Node*)>> load;            // loading data
		std::vector<std::function<void(Node*)>> save;            // saving data
		std::vector<std::function<void(Node*)>> pre_init;        // loading resources pre-object creation
		std::vector<std::function<void(Node*)>> init;            // loading resources
		std::vector<std::function<void(Node*)>> begin;           // moved from cache
		std::vector<std::function<void(Node*)>> on_enable;       // on enable
		std::vector<std::function<void(Node*)>> early_tick;      // start of a frame
		std::vector<std::function<void(Node*)>> tick;            // before the physics tick
		std::vector<std::function<void(Node*)>> post_physics;    // after the physics tick
		std::vector<std::function<void(Node*)>> late_tick;       // end of a frame
		std::vector<std::function<void(Node*)>> on_disable;      // on disable
		std::vector<std::function<void(Node*)>> end;             // moved to cache
		std::vector<std::function<void(Node*)>> destroy;         // moved to destroy
	} table;

	void load(Node& n);
	void save(Node& n);
	void preInit(Node& n);
	void init(Node& n);
	void begin(Node& n);
	void onEnable(Node& n);
	void earlyTick(Node& n);
	void tick(Node& n);
	void postPhysics(Node& n);
	void lateTick(Node& n);
	void onDisable(Node& n);
	void end(Node& n);
	void destroy(Node& n);
};

}
