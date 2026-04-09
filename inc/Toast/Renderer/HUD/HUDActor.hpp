/// @file HUDActor.hpp
/// @author dario
/// @date 08/04/2026.


#pragma once
#include "Toast/Objects/Actor.hpp"

#include <Ultralight/Ultralight.h>
#include <cstdint>
#include <string>
#include <vector>

namespace toast {
class HUDWorldRendererComponent;
}

class HUDActor : public toast::Actor {
	public:
	using ViewRef = ultralight::RefPtr<ultralight::View>;

	void Init() override;
	void Begin() override;
	void Tick() override;
	void Destroy() override;
	void OnEnable() override;
	void OnDisable() override;

	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	void SetUrl(const std::string& url);
	void ExecuteJS(const std::string& script);

	REGISTER_TYPE(HUDActor);

private:
	void EnsureRendererComponent();
	void CreateView();
	void DestroyView();
	void SyncResolvedTexture();
	void ApplyMeshScaleFromViewSize();

	toast::HUDWorldRendererComponent* m_worldRenderer = nullptr;
	ViewRef m_view;

	std::string m_url;
	std::vector<std::string> m_pendingScripts;
	uint32_t m_viewWidth = 1024;
	uint32_t m_viewHeight = 1024;
	bool m_scaleMeshByViewSize = true;
	float m_pixelToUnitScale = 0.01f;
	int m_sortOrder = 0;

	std::string m_meshPath = "MODELS/quad.obj";
	std::string m_materialPath = "MATERIALS/default.mat";
};