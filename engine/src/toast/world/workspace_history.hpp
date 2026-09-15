#pragma once

#include "workspace_events.hpp"

#include <functional>
#include <memory>
#include <toast/assets/prefab.hpp>

namespace toast {

class WorkspaceHistory {
public:
	using Snapshot = assets::Prefab;
	using Capture = std::function<Snapshot()>;
	using Restore = std::function<bool(const Snapshot&)>;

	struct Context {
		event::HistoryOperation operation = event::HistoryOperation::change_value;
		UID node;
		std::string node_name;
		std::string subject;
		std::string previous_value;
		std::string current_value;
	};

	WorkspaceHistory(uint64_t handle, Capture capture, Restore restore, bool available, bool initially_saved);
	~WorkspaceHistory();

	[[nodiscard]]
	auto available() const noexcept -> bool {
		return m_available;
	}

	[[nodiscard]]
	auto transactionOpen() const noexcept -> bool {
		return m_transaction.has_value();
	}

	[[nodiscard]]
	auto restoring() const noexcept -> bool {
		return m_restoring;
	}

	void begin(uint64_t transaction, Context context);
	void commit(uint64_t transaction = 0);
	void cancel(uint64_t transaction = 0);

	auto beginAtomic(Context context) -> bool;
	void finishAtomic(bool owned);

	void apply(const event::WorkspaceApplyHistorySnapshot& request);
	void prepareMerge(const event::WorkspacePrepareHistoryMerge& request);
	void resolve(const event::WorkspaceResolveHistoryConflicts& resolutions);

	void sendInitial() const;

private:
	struct Transaction {
		uint64_t id = 0;
		Context context;
		std::shared_ptr<const Snapshot> before;
		bool observed_mutation = false;
	};

	struct PendingMerge;

	uint64_t m_handle = 0;
	Capture m_capture;
	Restore m_restore;
	bool m_available = true;
	bool m_initially_saved = false;
	bool m_restoring = false;
	std::optional<Transaction> m_transaction;
	std::unique_ptr<PendingMerge> m_pending;

	[[nodiscard]]
	auto same(const Snapshot& a, const Snapshot& b) const -> bool;
	void beginMerge(
	    uint64_t request, bool merge, std::shared_ptr<const Snapshot> base, std::shared_ptr<const Snapshot> current,
	    std::shared_ptr<const Snapshot> incoming
	);
};

}
