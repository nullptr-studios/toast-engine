/**
 * @file test_nodes.hpp
 * @author Xein
 * @date 10 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "node_3d.hpp"

#include <toast/log.hpp>

namespace toast {

class [[ToastNode]] TOAST_API TestNode1 : public Node3D {
private:
	void earlyTick() { TOAST_TRACE("TestNode1", "EarlyTick"); }

	void tick() { TOAST_TRACE("TestNode1", "Tick"); }
};

class [[ToastNode]] TOAST_API TestNode2 : public Node {
private:
	void tick() { TOAST_TRACE("TestNode2", "Tick"); }
};

class [[ToastNode]] TOAST_API TestNode3 : public Node3D {
private:
	void tick() { TOAST_TRACE("TestNode3", "Tick"); }

	void lateTick() { TOAST_TRACE("TestNode3", "LateTick"); }
};

class [[ToastNode]] TOAST_API TestNode4 : public Node3D {
private:
	void tick() { TOAST_TRACE("TestNode4", "Tick"); }
};

class [[ToastNode]] TOAST_API TestNode5 : public Node3D {
private:
	void lateTick() { TOAST_TRACE("TestNode5", "LateTick"); }
};

}
