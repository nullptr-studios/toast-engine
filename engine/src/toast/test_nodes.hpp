/**
 * @file test_nodes.h
 * @author Xein
 * @date 06 Jul 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <toast/log.hpp>
#include <toast/world/node_3d.hpp>

namespace toast {

class [[ToastNode]] TestNode1 : public Node {
private:
	void earlyTick() { TOAST_TRACE("Test1", "Test 1 earlytick"); }

	void tick() { TOAST_TRACE("Test1", "Test 1 tick"); }
};

class [[ToastNode]] TestNode2 : public Node3D {
private:
	void tick() { TOAST_TRACE("Test2", "Test 2 tick"); }
};

class [[ToastNode]] PrintTextNode : public Node {
private:
	void preInit() { TOAST_TRACE("Debug", "preInit()"); }

	void init() { TOAST_TRACE("Debug", "init()"); }

	void load() { TOAST_TRACE("Debug", "load()"); }

	void begin() { TOAST_TRACE("Debug", "begin()"); }

	void onEnable() { TOAST_TRACE("Debug", "onEnable()"); }

	void onDisable() { TOAST_TRACE("Debug", "onDisable()"); }

	void earlyTick() { TOAST_TRACE("Debug", "earlyTick()"); }

	void tick() { TOAST_TRACE("Debug", "tick()"); }

	void postPhysics() { TOAST_TRACE("Debug", "postPhysics()"); }

	void lateTick() { TOAST_TRACE("Debug", "lateTick()"); }

	void end() { TOAST_TRACE("Debug", "end()"); }

	void destroy() { TOAST_TRACE("Debug", "destroy()"); }
};

}
