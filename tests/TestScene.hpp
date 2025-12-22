#pragma once

#include "Engine/Toast/Objects/Scene.hpp"
#include "TestActor.hpp"

class TestScene : public toast::Scene {
public:
	REGISTER_TYPE(TestScene);

	void Init() override {
		children.Add<TestActor>("TestActor");
	}

	void Begin() override { }
};
