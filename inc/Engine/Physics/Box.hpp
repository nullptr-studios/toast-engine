/// @file Box.hpp
/// @author Xein
/// @date 24 Dec 2025

#pragma once
#include "Engine/Core/Time.hpp"
#include "Engine/Input/InputListener.hpp"
#include "Engine/Renderer/DebugDrawLayer.hpp"
#include "Physics/PhysicsSystem.hpp"

#include <Engine/Toast/Objects/Actor.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

namespace physics {

class Box : public toast::Actor {
public:
	REGISTER_TYPE(Box);
	glm::vec2 size = { 10.0f, 10.0f };
	float rotation = 0.0f;
	float rotationSpeed = 3.14f;
	std::array<glm::vec2, 4> points;

	void Init() override {
		CalculatePoints();
		input.Subscribe1D("rotate", [this](const input::Action1D* a) {
				direction = a->value;
		});
		input::SetLayout("test");
		physics::PhysicsSystem::AddBox(this);
	}

	void Inspector() override {
		if (ImGui::DragFloat2("Size", &size.x)) {
			CalculatePoints();
		}

		float rot_deg = glm::degrees(rotation);
		if (ImGui::DragFloat("Rotation", &rot_deg)) {
			rotation = glm::radians(rot_deg);
			CalculatePoints();
		}

		ImGui::DragFloat("Rotate Speed", &rotationSpeed);
	}

	void EditorTick() override {
		renderer::DebugLine(points[0], points[1]);
		renderer::DebugLine(points[1], points[2]);
		renderer::DebugLine(points[2], points[3]);
		renderer::DebugLine(points[3], points[0]);

		if (direction != 0.0f) {
			rotation += direction * rotationSpeed * Time::delta();
			CalculatePoints();
		}
	}

private:
	input::Listener input;
	float direction;

	void CalculatePoints() {
		float s = std::sin(rotation);
		float c = std::cos(rotation);

		std::array<float, 4> x_coords = { -size.x, size.x, size.x, -size.x };
		std::array<float, 4> y_coords = { size.y, size.y, -size.y, -size.y };

		for (int i = 0; i < 4; i++) {
			float px = x_coords[i];
			float py = y_coords[i];

			points[i].x = (px * c) - (py * s);
			points[i].y = (px * s) + (py * c);
		}
	}
};

}
