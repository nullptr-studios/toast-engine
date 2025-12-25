/// @file Collider.hpp
/// @author Xein
/// @date 25 Dec 2025

#pragma once
#include <Engine/Toast/Components/Component.hpp>

namespace physics {

class Collider : public toast::Component {
public:
	REGISTER_TYPE(Collider);

	void Inspector() override;
	void EditorTick() override;

private:
	void AddPoint();

	struct M {
		unsigned pointCount;
	} m;
};

}
