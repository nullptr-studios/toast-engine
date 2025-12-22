// @file App.hpp
/// @author Xein
/// @date 21 Dec 2025

#pragma once
#include <Engine/Core/Entrypoint.hpp>
#include <Engine/Toast/Engine.hpp>

class Test final : public toast::Engine {
	void Begin() override;
};

toast::Engine* toast::CreateApplication() {
	return new Test();
}
