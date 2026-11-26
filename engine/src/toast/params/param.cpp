#include "param.hpp"

#include "group.hpp"
#include "registry.hpp"

#include <toast/log.hpp>

namespace toast::params {

ParamBase::ParamBase(const Info& info)
    : m_name(info.name),
      m_description(info.description),
      m_kind(info.kind),
      m_options(info.options),
      m_requires_restart(info.requires_restart),
      m_asset_type(info.asset_type),
      m_unit(info.unit) {
	const std::string prefix = detail::prefixFor(this);
	if (prefix.empty()) {
		m_id = std::string {info.id.text};
		m_hash = info.id.hash;
	} else {
		m_id = prefix + "." + std::string {info.id.text};
		m_hash = fnv1a(m_id);
	}

	m_group = detail::locateGroup(this);
	if (m_group != nullptr && !m_group->add(*this)) {
		m_group = nullptr;
	}
}

ParamBase::~ParamBase() {
	if (m_group != nullptr) {
		m_group->remove(*this);
	}
}

void ParamBase::onChangeValue(std::function<void(const Value&)> callback) {
	m_callbacks.push_back(std::move(callback));
}

auto ParamBase::setLayer(Layer layer, std::optional<Value> value) -> bool {
	const bool acceptable = !value.has_value() || accepts(*value);
	m_layers[static_cast<size_t>(layer)] = std::move(value);
	resolve();
	return acceptable;
}

void ParamBase::request(Value value, Layer layer) {
	if (m_group == nullptr) {
		TOAST_WARN("Params", "'{}' is not in a group so it cannot be requested", m_id);
		return;
	}
	Registry::get().request(std::string {m_group->name()} + "." + m_id, std::move(value), layer);
}

void ParamBase::resolve() {
	constexpr std::array<Layer, k_layer_count> priority {Layer::runtime, Layer::saved, Layer::project};
	for (const Layer layer : priority) {
		if (layer == Layer::project && m_kind == Kind::user) {
			continue;
		}
		const auto& stored = m_layers[static_cast<size_t>(layer)];
		if (stored.has_value() && apply(*stored)) {
			return;
		}
	}
	apply(defaultValue());
}

auto ParamBase::snapshot() const -> Value {
	const std::lock_guard lock(m_snapshot_mutex);
	return m_snapshot;
}

void ParamBase::publish(Value current) {
	const std::lock_guard lock(m_snapshot_mutex);
	m_snapshot = std::move(current);
}

void ParamBase::notify() {
	if (m_callbacks.empty()) {
		return;
	}
	const Value current = value();
	for (const auto& callback : m_callbacks) {
		callback(current);
	}
}

}
