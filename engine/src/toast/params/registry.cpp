#include "registry.hpp"

#include <toast/log.hpp>

namespace toast::params {

auto Registry::get() -> Registry& {
	static Registry instance;
	return instance;
}

auto Registry::groups() const -> std::vector<GroupBase*> {
	return detail::liveGroups();
}

auto Registry::findGroup(std::string_view name) const -> GroupBase* {
	for (GroupBase* group : detail::liveGroups()) {
		if (group->name() == name) {
			return group;
		}
	}
	return nullptr;
}

auto Registry::find(std::string_view path) const -> ParamBase* {
	const size_t dot = path.find('.');
	if (dot == std::string_view::npos) {
		return nullptr;
	}
	const GroupBase* group = findGroup(path.substr(0, dot));
	return group != nullptr ? group->find(path.substr(dot + 1)) : nullptr;
}

void Registry::request(std::string path, Value value, Layer layer) {
	const std::lock_guard lock(m_mutex);
	m_pending.push_back({std::move(path), std::move(value), layer});
}

void Registry::setSaveHandler(std::function<void()> handler) {
	const std::lock_guard lock(m_mutex);
	m_save_handler = std::move(handler);
}

void Registry::setRestoreHandler(std::function<void()> handler) {
	const std::lock_guard lock(m_mutex);
	m_restore_handler = std::move(handler);
}

void Registry::requestRestore() {
	m_restore_requested = true;
}

void Registry::requestSave() {
	m_save_requested = true;
}

void Registry::setLayerPath(Layer layer, std::string path) {
	const std::lock_guard lock(m_mutex);
	m_layer_paths[static_cast<size_t>(layer)] = std::move(path);
}

auto Registry::layerPath(Layer layer) const -> std::string {
	const std::lock_guard lock(m_mutex);
	return m_layer_paths[static_cast<size_t>(layer)];
}

void Registry::requestClear(std::string path, Layer layer) {
	const std::lock_guard lock(m_mutex);
	m_pending.push_back({std::move(path), std::nullopt, layer});
}

auto Registry::pendingValue(std::string_view path) const -> std::optional<Value> {
	const std::lock_guard lock(m_mutex);
	for (auto it = m_pending.rbegin(); it != m_pending.rend(); ++it) {
		if (it->path == path) {
			return it->value;
		}
	}
	return std::nullopt;
}

auto Registry::applyPending() -> size_t {
	m_apply_thread = std::this_thread::get_id();
	++m_passes_started;

	std::vector<Pending> batch;
	{
		const std::lock_guard lock(m_mutex);
		batch.swap(m_pending);
	}

	size_t applied = 0;
	for (auto& item : batch) {
		ParamBase* param = find(item.path);
		if (param == nullptr) {
			if (m_reported.insert(item.path).second) {
				TOAST_WARN("Params", "No parameter '{}'", item.path);
			}
			continue;
		}
		const Layer layer = param->kind() == Kind::user && item.layer == Layer::project ? Layer::saved : item.layer;
		if (!param->setLayer(layer, std::move(item.value))) {
			TOAST_WARN("Params", "'{}' does not take that value", item.path);
			continue;
		}
		if (layer != Layer::runtime && param->group() != nullptr) {
			param->group()->markDirty(layer);
		}
		++applied;
	}

	if (m_restore_requested.exchange(false)) {
		std::function<void()> handler;
		{
			const std::lock_guard lock(m_mutex);
			handler = m_restore_handler;
		}
		if (handler) {
			handler();
		}
	}
	if (m_save_requested.exchange(false)) {
		std::function<void()> handler;
		{
			const std::lock_guard lock(m_mutex);
			handler = m_save_handler;
		}
		if (handler) {
			handler();
		}
	}
	++m_passes_finished;
	return applied;
}

auto Registry::waitForApply(std::chrono::milliseconds timeout) -> bool {
	if (std::this_thread::get_id() == m_apply_thread.load()) {
		applyPending();
		return true;
	}

	const uint64_t wanted = m_passes_started.load() + 1;
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (m_passes_finished.load() < wanted) {
		if (std::chrono::steady_clock::now() >= deadline) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return true;
}

void Registry::setAuthoringLayer(Layer layer) {
	m_authoring = layer == Layer::project ? Layer::project : Layer::saved;
}

auto Registry::commit(ParamBase& param, Value value) -> bool {
	if (!param.accepts(value)) {
		return false;
	}
	const Layer layer = param.kind() == Kind::user ? Layer::saved : m_authoring;
	param.setLayer(layer, std::move(value));
	if (param.group() != nullptr) {
		param.group()->markDirty(layer);
	}
	return true;
}

void Registry::clearRuntime() {
	for (GroupBase* group : detail::liveGroups()) {
		for (ParamBase* param : group->params()) {
			if (param->layerValue(Layer::runtime).has_value()) {
				param->setLayer(Layer::runtime, std::nullopt);
			}
		}
	}
}

auto Registry::dirtyGroups() const -> std::vector<DirtyEntry> {
	std::vector<DirtyEntry> result;
	for (GroupBase* group : detail::liveGroups()) {
		for (const Layer layer : {Layer::project, Layer::saved}) {
			if (group->dirty(layer)) {
				result.push_back({group, layer});
			}
		}
	}
	return result;
}

}
