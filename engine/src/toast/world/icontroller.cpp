#include "icontroller.hpp"

#include <toast/input/input_events.hpp>
#include <toast/input/keycodes.hpp>

namespace input {

void IController::init() {
	listener().subscribe<event::LastInputType>([this](const event::LastInputType& e) {
		if (!participatesIn(toast::NodeOwnerParticipation::runtime_input)) {
			return false;
		}
		m_last_input_name = e.name;
		switch (e.device) {
			case Device::keyboard: m_last_input_type = "keyboard"; break;
			case Device::mouse: m_last_input_type = "mouse"; break;
			case Device::controller: m_last_input_type = "controller"; break;
			default: m_last_input_type = "none"; break;
		}
		return false;
	});
}

}
