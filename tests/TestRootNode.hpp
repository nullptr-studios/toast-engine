#pragma once

#include "TestActor.hpp"

#include <Toast/Objects/RootNode.hpp>

class TestRootNode : public toast::RootNode {
public:
	REGISTER_TYPE(TestRootNode);

	void Init() override {
		children.Add<TestActor>("TestActor");
	}

	void Begin() override { }
};
