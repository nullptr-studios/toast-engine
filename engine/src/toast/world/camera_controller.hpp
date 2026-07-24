/**
 * @file CameraController.hpp
 * @author Xein
 * @date 22 Jul 2026
 *
 * @brief Manages switching between different active cameras
 */

#pragma once
#include "camera.hpp"

#include <glm/glm.hpp>
#include <limits>
#include <toast/export.hpp>
#include <toast/world/node.hpp>

namespace toast {

class Node3D;

enum class TransitionType : uint8_t {
	linear = 0,
	spherical = 1,
};

/**
 * @brief Manages switching between different active cameras
 *
 * By default the World will just pick whatever camera was activated first until that camera
 * is deactivated and only then it will search for the next available camera
 *
 * However, if more control over the cameras is needed on the project, this node will
 * override the default engine behaviour allowing to select the current camera and its
 * transition method
 *
 * Switching cameras can also be done by sending an event
 */
class [[ToastNode, Icon("Camera")]] TOAST_API CameraController : public Node {
private:
	[[Reflect, ReadOnly, Name("Active Camera")]]
	std::string m_active_camera_name;

public:
	[[Reflect]]
	void setActiveCamera(Box<Camera> c);

	[[Reflect]]
	void setActiveCamera(UID uid);

	[[Reflect]]
	void setActiveCamera(std::string_view name);

	[[Reflect]]
	auto getActiveCamera() -> Box<Camera>;

	[[Reflect]]
	auto getActiveCameraIndex() -> uint8_t;

	[[Reflect]]
	bool do_transition = false;

	[[Reflect, Enum("Linear", "Spherical")]]
	TransitionType transition_type = TransitionType::linear;

	[[Reflect, Unit("s")]]
	float transition_time = 1.0f;

	[[Reflect, Unit("%"), Range(0, 50)]]
	float ease_in = 0.0f;

	[[Reflect, Unit("%"), Range(0, 50)]]
	float ease_out = 0.0f;

	[[Reflect, Name("Look At Target")]]
	Box<Node3D> look_target;

	[[Reflect, Button("Previous Camera")]]
	void prev();

	[[Reflect, Button("Next Camera")]]
	void next();

	/**
	 * @note This will be called by the World automatically but it's here in case
	 *       we need to call it manually for some reason
	 */
	void addCamera(Camera& camera);
	void removeCamera(Camera& camera);
	void clearCameras();

private:
	static constexpr uint8_t no_active_camera = std::numeric_limits<uint8_t>::max();

	std::vector<Box<Camera>> m_cameras;
	uint8_t m_active_camera_index = no_active_camera;

	Box<Camera> m_transition_camera;
	glm::vec3 m_transition_start = glm::vec3(0.0f);
	glm::vec3 m_transition_end = glm::vec3(0.0f);
	glm::vec3 m_transition_center = glm::vec3(0.0f);
	float m_transition_elapsed = 0.0f;
	float m_transition_duration = 0.0f;
	float m_transition_ease_in = 0.0f;
	float m_transition_ease_out = 0.0f;
	TransitionType m_transition_type = TransitionType::linear;
	bool m_transition_active = false;

	void init();
	void begin();
	void end();
	void onEnable();
	void onDisable();
	void tick();
	void stopTransition(bool restore_camera_position);
};

}
