/// @file ForceLink.cpp
/// @author dario
/// @date 20/01/2026.

// This file exists solely to force the linker to force include objects/components

// Objects
#include <Toast/Objects/Actor.hpp>
#include <Toast/Objects/Object.hpp>
#include <Toast/Objects/ParticleSystem.hpp>
#include <Toast/Objects/Scene.hpp>

// Renderer
#include <Toast/Renderer/Camera.hpp>
#include <Toast/Renderer/Lights/2DLight.hpp>
#include <Toast/Renderer/Lights/GlobalLight.hpp>

// Components
#include <Toast/Components/AtlasRendererComponent.hpp>
#include <Toast/Components/MeshRendererComponent.hpp>
#include <Toast/Components/SpineRendererComponent.hpp>
#include <Toast/Components/TransformComponent.hpp>
