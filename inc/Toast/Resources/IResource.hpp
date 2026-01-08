/// @file IResource.hpp
/// @author dario
/// @date 23/09/2025.

#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace resource {

///@enum ResourceType
///@brief Represents the type of resource
enum class ResourceType : uint8_t {
	TEXTURE,
	MODEL,
	AUDIO,
	SHADER,
	MATERIAL,
	FONT,
	SPINE_ATLAS,
	SPINE_SKELETON_DATA,
	UNKNOWN
};

///@enum ResourceState
///@brief Represents the state of a resource
enum class ResourceState : uint8_t {
	UNLOADED,
	LOADING,
	LOADEDCPU,
	UPLOADING,
	UPLOADEDGPU,
	FAILED
};

}

///@class IResource
///@brief Base class for all resources
class IResource {
public:
	IResource() = default;

	IResource(std::string path, resource::ResourceType type = resource::ResourceType::UNKNOWN, bool gpu = false)
	    : m_path(std::move(path)),
	      m_gpu(gpu),
	      m_resourceType(type) { }

	virtual ~IResource() = default;

	// non-copyable
	IResource(const IResource&) = delete;
	IResource& operator=(const IResource&) = delete;

	// movable
	IResource(IResource&& other) noexcept
	    : m_path(std::move(other.m_path)),
	      m_gpu(other.m_gpu),
	      m_resourceType(other.m_resourceType),
	      m_resourceState(other.m_resourceState) {
		other.m_gpu = false;
		other.m_resourceType = resource::ResourceType::UNKNOWN;
		other.m_resourceState = resource::ResourceState::UNLOADED;
	}

	IResource& operator=(IResource&& other) noexcept {
		if (this != &other) {
			m_path = std::move(other.m_path);
			m_gpu = other.m_gpu;
			m_resourceType = other.m_resourceType;
			m_resourceState = other.m_resourceState;
			other.m_gpu = false;
			other.m_resourceType = resource::ResourceType::UNKNOWN;
			other.m_resourceState = resource::ResourceState::UNLOADED;
		}
		return *this;
	}

	///@brief loads the resource into CPU memory
	virtual void Load() { }

	///@brief loads the resource into GPU memory, only if m_gpu is true
	virtual void LoadMainThread() { }    // For loading stuff that needs to be done on the main thread (like OpenGL stuff) -Dario

	[[nodiscard]]
	resource::ResourceType GetResourceType() const {
		return m_resourceType;
	}

	[[nodiscard]]
	resource::ResourceState GetResourceState() const {
		return m_resourceState;
	}

	void SetResourceState(resource::ResourceState state) {
		m_resourceState = state;
	}

	[[nodiscard]]
	bool IsGPU() const {
		return m_gpu;
	}

protected:
	std::string m_path;

private:
	bool m_gpu = false;
	resource::ResourceType m_resourceType = resource::ResourceType::UNKNOWN;
	resource::ResourceState m_resourceState = resource::ResourceState::UNLOADED;
};
