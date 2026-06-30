/**
 * @file audio_listener.hpp
 * @author Xein
 * @date 30 Jun 2026
 *
 * @brief A node that can listen to all the audio events
 */

#pragma once
#include <toast/export.hpp>
#include <toast/world/node_3d.hpp>

namespace toast {

class TOAST_API [[ToastNode, Color("Beige"), Icon("AudioListener")]] AudioListener : public Node3D {
public:
	void index(int value);
	void weight(float value);

private:
	void begin();
	void lateTick();
	
	[[Reflect, Range(0, 7)]]
	int m_index = 0; ///< Fmod listener index for multiplayer
	
	[[Reflect, Range(0.0, 1.0)]]
	float m_weight = 1.0f; ///< How much this listener contributes to the position
	
	[[Reflect]]
	Box<Node3D> attenuation_override;
	
	bool is_dirty = true;
	bool m_weight_changed = false;
	
	glm::vec3 prev_position;
	glm::vec3 prev_rotation;
};

}
