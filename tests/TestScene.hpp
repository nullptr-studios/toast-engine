#pragma once

#include "TestActor.hpp"

#include <Toast/Objects/Scene.hpp>

class TestScene : public toast::Scene {
public:
	REGISTER_TYPE(TestScene);

	void Init() override {
		children.Add<TestActor>("TestActor");
	}

	void Begin() override { }
};
