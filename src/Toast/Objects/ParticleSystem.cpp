#include "Toast/Objects/ParticleSystem.hpp"

#include "Toast/Log.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/ParticleEmitter.hpp"
#include "Toast/Renderer/ParticleSystemManager.hpp"
#include "Toast/Time.hpp"

#include <algorithm>
#include <string>
#include <vector>

#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

namespace toast {

// renderable wrapper
class ParticleSystemRenderable : public renderer::IRenderable {
public:
	explicit ParticleSystemRenderable(renderer::ParticleSystemManager* mgr) : m_mgr(mgr) { }

	void OnRender(const glm::mat4& viewProjection) noexcept override {
		if (m_mgr) {
			m_mgr->Render(viewProjection);
		}
	}

private:
	renderer::ParticleSystemManager* m_mgr;
};

ParticleSystem::ParticleSystem() { }

ParticleSystem::~ParticleSystem() = default;

void ParticleSystem::Init() {
	Actor::Init();
	m_manager = std::make_unique<renderer::ParticleSystemManager>();
	m_manager->OnAttach();

	// create and register renderable wrapper
	m_renderable = std::make_unique<ParticleSystemRenderable>(m_manager.get());
	renderer::IRendererBase::GetInstance()->AddRenderable(m_renderable.get());

	// Create a default looping smoke emitter and start it
	renderer::ParticleEmitter* e = CreateEmitter();
	if (e) {
		// Smoke default
		e->position = glm::vec3(0.0f, 0.0f, 0.0f);
		e->startVelocity = glm::vec3(0.0f, 0.5f, 0.0f);
		e->startLife = 3.0f;
		e->startSize = 0.15f;
		e->endSize = 1.0f;
		e->startColor = glm::vec4(0.8f, 0.8f, 0.8f, 0.6f);
		e->endColor = glm::vec4(0.2f, 0.2f, 0.2f, 0.0f);
		e->loop = true;
		e->SetRate(50.0f);
		e->Start();
	}
}

void ParticleSystem::Load(json_t /*j*/, bool /*force_create*/) {
	//@TODO
}

void ParticleSystem::Destroy() {
	// unregister renderable
	if (m_renderable) {
		renderer::IRendererBase::GetInstance()->RemoveRenderable(m_renderable.get());
		m_renderable.reset();
	}

	if (m_manager) {
		m_manager->OnDetach();
		m_manager.reset();
	}
	Actor::Destroy();
}

void ParticleSystem::Tick() {
	Actor::Tick();
	if (m_manager)
		m_manager->Tick(static_cast<float>(Time::delta()));
}

renderer::ParticleEmitter* ParticleSystem::CreateEmitter(uint32_t maxParticles) {
	if (!m_manager)
		return nullptr;
	
	return m_manager->CreateEmitter(maxParticles);
}

renderer::ParticleSystemManager* ParticleSystem::GetManager() const {
	return m_manager.get();
}

#ifdef TOAST_EDITOR
void ParticleSystem::Inspector() {
	Actor::Inspector();
	ImGui::Separator();
	ImGui::Text("Particle System Settings");
	
	// gravity
	glm::vec3 g = m_manager ? m_manager->GetGravity() : glm::vec3(0.0f);
	if (ImGui::InputFloat3("Gravity", &g.x)) {
		if (m_manager)
			m_manager->SetGravity(g);
	}
	// expose max particles and readback interval
	int maxp = static_cast<int>(m_manager ? m_manager->GetMaxParticles() : 65536);
	if (ImGui::InputInt("Max Particles", &maxp)) {
		if (m_manager)
			m_manager->SetMaxParticles(std::max(1, maxp));
	}
	int readback = m_manager ? m_manager->GetReadbackInterval() : 5;
	if (ImGui::InputInt("Readback Interval", &readback)) {
		if (m_manager)
			m_manager->SetReadbackInterval(readback);
	}

	if (!m_manager)
		return;

	ImGui::Separator();
	ImGui::Text("Emitters (%d)", static_cast<int>(m_manager->GetEmitters().size()));

	// Add new emitter
	if (ImGui::Button("Add Emitter")) {
		auto* e = CreateEmitter();
		if (e)
			e->Start();
	}
	ImGui::SameLine();
	if (ImGui::Button("Remove All")) {
		// destroy all emitters
		std::vector<renderer::ParticleEmitter*> toRemove;
		for (auto* em : m_manager->GetEmitters()) {
			toRemove.push_back(em);
		}
		for (auto* em : toRemove) {
			m_manager->DestroyEmitter(em);
		}
	}

	int idx = 0;
	for (auto* em : m_manager->GetEmitters()) {
		ImGui::PushID(idx);
		std::string header = "Emitter ";
		if (em) {
			header += std::to_string(em->GetId());
		}
		if (ImGui::CollapsingHeader(header.c_str())) {
			
			// position
			ImGui::InputFloat3("Position", &em->position.x);
			
			// rate
			float rate = em->GetRate();
			if (ImGui::DragFloat("Rate", &rate, 1.0f, 0.0f, 10000.0f))
				em->SetRate(rate);
			
			// life
			ImGui::InputFloat("Start Life", &em->startLife);
			
			// sizes
			ImGui::InputFloat("Start Size", &em->startSize);
			ImGui::InputFloat("End Size", &em->endSize);
			
			// colors
			ImGui::ColorEdit4("Start Color", &em->startColor.r);
			ImGui::ColorEdit4("End Color", &em->endColor.r);
			
			// loop/burst control
			ImGui::Checkbox("Loop", &em->loop);
			int burstVal = static_cast<int>(em->burstCount);
			if (ImGui::InputInt("Burst Count", &burstVal)) {
				burstVal = std::max(0, burstVal);
				em->burstCount = static_cast<uint32_t>(burstVal);
			}
			if (ImGui::Button("Trigger Burst")) {
				em->TriggerBurst();
			}
			ImGui::SameLine();
			if (em->loop) {
				if (ImGui::Button("Start")) {
					em->Start();
				}
				ImGui::SameLine();
				if (ImGui::Button("Stop")) {
					em->Stop();
				}
			} else {
				// non-looping one-shot
				ImGui::Text("One-shot burst: %s", em->burstCount > 0 ? "configured" : "not configured");
			}

			// Remove emitter
			if (ImGui::Button("Remove Emitter")) {
				m_manager->DestroyEmitter(em);
				ImGui::PopID();
				break;
			}
		}
		ImGui::PopID();
		idx++;
	}
}
#endif

}    // namespace toast
