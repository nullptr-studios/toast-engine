#pragma once

#include "Engine/Physics/Rigidbody.hpp"
#include "Engine/Renderer/DebugDrawLayer.hpp"
#include "Engine/Toast/Components/MeshRendererComponent.hpp"
#include "Engine/Toast/Objects/Actor.hpp"
#include "glm/fwd.hpp"

class TestActor : public toast::Actor {
public:
	REGISTER_TYPE(TestActor)

	void Init() override {
		Actor::Init();
		rb = children.Add<physics::Rigidbody>("Rigidbody");
		mesh = children.Add<toast::MeshRendererComponent>("Mesh");
	}

	void Begin() override {
		Actor::Begin();
		mesh->SetMesh("models/quad.obj");
		mesh->SetMaterial("shaders/default.shader");

		transform()->position({ 0.0f, 0.0f, 0.0f });
	}

	void Tick() override {
		Actor::Tick();
		glm::vec2 pos = { transform()->worldPosition().x, transform()->worldPosition().y };
		renderer::DebugCircle(pos, 1.0f, { 1.0f, 0.0f, 0.0f, 1.0f });
	}

private:
	physics::Rigidbody* rb;
	toast::MeshRendererComponent* mesh;
};
