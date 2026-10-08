#include "tree_lock.hpp"

#include "node.hpp"
#include "node_owner.hpp"

#include <toast/log.hpp>

namespace toast {

namespace {

thread_local int t_held = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void entering() noexcept {
	TOAST_ASSERT(t_held == 0, "World", "A thread asked for the lock of a node tree while it holds one");
	++t_held;
}

void leaving() noexcept {
	--t_held;
}

}

TreeReadLock::TreeReadLock(const INodeOwner* owner) noexcept : m_owner(owner) {
	if (m_owner != nullptr) {
		entering();
		m_owner->treeMutex().lock_shared();
	}
}

TreeReadLock::TreeReadLock(const Node& node) noexcept : TreeReadLock(node.owner()) { }

TreeReadLock::~TreeReadLock() {
	if (m_owner != nullptr) {
		m_owner->treeMutex().unlock_shared();
		leaving();
	}
}

TreeWriteLock::TreeWriteLock(const INodeOwner* owner) noexcept : m_owner(owner) {
	if (m_owner != nullptr) {
		entering();
		m_owner->treeMutex().lock();
	}
}

TreeWriteLock::TreeWriteLock(const Node& node) noexcept : TreeWriteLock(node.owner()) { }

TreeWriteLock::~TreeWriteLock() {
	if (m_owner != nullptr) {
		m_owner->treeMutex().unlock();
		leaving();
	}
}

}
