/**
 * @file tree_lock.hpp
 * @author Xein
 * @date 05 Oct 2026
 * @brief Access to the shape of an owner's node tree from more than one thread
 */

#pragma once

#include <toast/export.hpp>

namespace toast {

class INodeOwner;
class Node;

/// Shared access to the shape of the tree of an owner
class TOAST_API TreeReadLock {
public:
	explicit TreeReadLock(const INodeOwner* owner) noexcept;
	explicit TreeReadLock(const Node& node) noexcept;
	~TreeReadLock();

	TreeReadLock(const TreeReadLock&) = delete;
	auto operator=(const TreeReadLock&) -> TreeReadLock& = delete;
	TreeReadLock(TreeReadLock&&) = delete;
	auto operator=(TreeReadLock&&) -> TreeReadLock& = delete;

private:
	const INodeOwner* m_owner;
};

/// Exclusive access to the shape of the tree of an owner
class TOAST_API TreeWriteLock {
public:
	explicit TreeWriteLock(const INodeOwner* owner) noexcept;
	explicit TreeWriteLock(const Node& node) noexcept;
	~TreeWriteLock();

	TreeWriteLock(const TreeWriteLock&) = delete;
	auto operator=(const TreeWriteLock&) -> TreeWriteLock& = delete;
	TreeWriteLock(TreeWriteLock&&) = delete;
	auto operator=(TreeWriteLock&&) -> TreeWriteLock& = delete;

private:
	const INodeOwner* m_owner;
};

}
