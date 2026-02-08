#pragma once

#include "glm/fwd.hpp"

#include <Toast/Components/MeshRendererComponent.hpp>
#include <Toast/Objects/Actor.hpp>
#include <Toast/Physics/Rigidbody.hpp>
#include <Toast/Renderer/DebugDrawLayer.hpp>

class TestActor : public toast::Actor {
public:
	REGISTER_TYPE(TestActor)

	void Init() override {
		rb = children.Add<physics::Rigidbody>("Rigidbody");
		mesh = children.Add<toast::MeshRendererComponent>("Mesh");
		
		
	}

	void Begin() override {
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
