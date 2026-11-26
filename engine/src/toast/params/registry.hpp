/**
 * @file registry.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Finds groups by name and moves writes from any
 */

#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <toast/export.hpp>
#include <toast/params/group.hpp>
#include <toast/params/param.hpp>
#include <unordered_set>
#include <vector>

namespace toast::params {

struct DirtyEntry {
	GroupBase* group;
	Layer layer;
};

class TOAST_API Registry {
public:
	static auto get() -> Registry&;

	[[nodiscard]]
	auto groups() const -> std::vector<GroupBase*>;

	[[nodiscard]]
	auto findGroup(std::string_view name) const -> GroupBase*;

	[[nodiscard]]
	auto find(std::string_view path) const -> ParamBase*;

	void request(std::string path, Value value, Layer layer = Layer::runtime);

	void requestClear(std::string path, Layer layer);

	[[nodiscard]]
	auto pendingValue(std::string_view path) const -> std::optional<Value>;

	auto applyPending() -> size_t;

	void setSaveHandler(std::function<void()> handler);

	void requestSave();

	void setRestoreHandler(std::function<void()> handler);

	void requestRestore();

	void setLayerPath(Layer layer, std::string path);

	[[nodiscard]]
	auto layerPath(Layer layer) const -> std::string;

	auto waitForApply(std::chrono::milliseconds timeout) -> bool;

	void setAuthoringLayer(Layer layer);

	[[nodiscard]]
	auto authoringLayer() const -> Layer {
		return m_authoring;
	}

	auto commit(ParamBase& param, Value value) -> bool;

	void clearRuntime();

	[[nodiscard]]
	auto dirtyGroups() const -> std::vector<DirtyEntry>;

private:
	struct Pending {
		std::string path;
		std::optional<Value> value;
		Layer layer;
	};

	mutable std::mutex m_mutex;
	std::vector<Pending> m_pending;
	std::unordered_set<std::string> m_reported;
	Layer m_authoring = Layer::saved;
	std::function<void()> m_save_handler;
	std::atomic<bool> m_save_requested {false};
	std::function<void()> m_restore_handler;
	std::atomic<bool> m_restore_requested {false};
	std::array<std::string, k_layer_count> m_layer_paths;
	std::atomic<uint64_t> m_passes_started {0};
	std::atomic<uint64_t> m_passes_finished {0};
	std::atomic<std::thread::id> m_apply_thread {};
};

}
