#pragma once

#include "Toast/Objects/Actor.hpp"
#include "Toast/Renderer/ParticleEmitter.hpp"

#include <memory>
#include <optional>

namespace renderer {
class ParticleSystemManager;
class IRenderable;
}

namespace toast {

class ParticleSystem : public Actor {
public:
	REGISTER_TYPE(ParticleSystem);

	ParticleSystem();
	~ParticleSystem() override;

	void Init() override;
	void Destroy() override;
	void Tick() override;

	//@TODO
	void Load(json_t j, bool force_create = true) override;

	// Create emitter via internal manager
	renderer::ParticleEmitter* CreateEmitter(uint32_t maxParticles = 65536);

	// Expose manager to components
	renderer::ParticleSystemManager* GetManager() const;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	std::unique_ptr<renderer::ParticleSystemManager> m_manager;
	std::unique_ptr<renderer::IRenderable> m_renderable;    // wrapper registered to renderer
};

}    // namespace toast
