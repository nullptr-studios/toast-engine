#include "ui_event_listener.hpp"

#include "../log.hpp"
#include "../world/node.hpp"
#include "ui_system.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/EventListenerInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <cctype>

namespace ui {

auto parseCallName(std::string_view value) -> std::string {
	size_t start = 0;
	while (start < value.size() && (std::isspace(static_cast<unsigned char>(value[start])) != 0)) {
		start++;
	}
	if (start >= value.size() || (!(std::isalpha(static_cast<unsigned char>(value[start])) != 0) && value[start] != '_')) {
		return {};
	}
	size_t end = start;
	while (end < value.size() && ((std::isalnum(static_cast<unsigned char>(value[end])) != 0) || value[end] == '_')) {
		end++;
	}
	size_t cursor = end;
	while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
		cursor++;
	}
	if (cursor >= value.size() || value[cursor++] != '(') {
		return {};
	}
	while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
		cursor++;
	}
	if (cursor >= value.size() || value[cursor++] != ')') {
		return {};
	}
	while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
		cursor++;
	}
	return cursor == value.size() ? std::string(value.substr(start, end - start)) : std::string();
}

void dispatchNodeCall(Rml::Context* context, std::string_view method) {
	if (context == nullptr || method.empty()) {
		return;
	}

	toast::Node* owner = UISystem::exists() ? UISystem::get().ownerForContext(context) : nullptr;
	if (owner == nullptr) {
		TOAST_WARN("UI", "UI event '{}' has no owning panel", method);
		return;
	}
	if (!owner->participatesIn(toast::NodeOwnerParticipation::runtime_input)) {
		return;
	}
	if (!owner->hasCallable(method)) {
		TOAST_WARN("UI", "UI event '{}' has no matching C++ or Lua method on panel '{}'", method, owner->name());
		return;
	}

	owner->call<void>(method);
}

namespace {

class NodeCallListener : public Rml::EventListener {
public:
	explicit NodeCallListener(std::string method) : m_method(std::move(method)) { }

	void ProcessEvent(Rml::Event& event) override {
		Rml::Element* element = event.GetCurrentElement();
		dispatchNodeCall(element != nullptr ? element->GetContext() : nullptr, m_method);
	}

	void OnDetach(Rml::Element* /*element*/) override { delete this; }

private:
	std::string m_method;
};

class NodeCallListenerInstancer : public Rml::EventListenerInstancer {
public:
	auto InstanceEventListener(const Rml::String& value, Rml::Element* /*element*/) -> Rml::EventListener* override {
		std::string method = parseCallName(value);
		if (method.empty()) {
			TOAST_WARN("UI", "Ignoring invalid inline UI event call '{}'; expected Method()", value);
			return nullptr;
		}
		return new NodeCallListener(std::move(method));
	}
};

}

void installEventListenerInstancer() {
	static NodeCallListenerInstancer instancer;
	Rml::Factory::RegisterEventListenerInstancer(&instancer);
	TOAST_TRACE("UI", "Registered node-call event listener instancer");
}

}
