/// @file ForceLink.cpp
/// @author dario
/// @date 20/01/2026.

// This file exists solely to force the linker to force include objects/components

// Nodes
#include <Toast/Nodes/Node3D.hpp>
#include <Toast/Nodes/Node.hpp>
#include <Toast/Nodes/ParticleSystem.hpp>
#include <Toast/Nodes/RootNode.hpp>

// Renderer
#include <Toast/Renderer/Camera.hpp>
#include <Toast/Renderer/Lights/2DLight.hpp>
#include <Toast/Renderer/Lights/GlobalLight.hpp>

// SubNodes
#include <Toast/SubNodes/AtlasRendererSubNode.hpp>
#include <Toast/SubNodes/MeshRendererSubNode.hpp>
#include <Toast/SubNodes/SpineRendererSubNode.hpp>
#include <Toast/SubNodes/TransformSubNode.hpp>
#include <Toast/Physics/Collider.hpp>
#include <Toast/Physics/Rigidbody.hpp>
