/// @file Pool.hpp
/// @author Xein
/// @date 16 Feb 2026

#pragma once
#include "Objects/Actor.hpp"
#include "Objects/Scene.hpp"

template<typename T>
concept is_object = std::is_base_of_v<toast::Object, T>;


template<is_object T, int size>
class Pool : public toast::Actor {
public:
	REGISTER_ABSTRACT(Pool);

	void Init() override {
		toast::Actor::Init();

		SetSerialize(false);

		for (int i = 0; i < size; i++) {
			m_pool[i] = children.Add<T>();
			m_pool[i]->SetSerialize(false);
			if constexpr (std::is_base_of_v<toast::Actor, T>) {
				m_pool[i]->transform()->position({ 99999.0f, 99999.0f, 0.0f });
			}
			m_free.emplace(m_pool[i]);
		}
	}

	void Begin() override {

		for (int i = 0; i < size; i++) {
			m_pool[i]->enabled(false);
		}
	}

	auto Release() -> T* {
		if (m_free.empty()) {
			TOAST_ERROR("Pool exhausted — no free objects");
			return nullptr;
		}
		auto* obj = m_free.top();
		m_free.pop();
		if (!obj->enabled()) {
			obj->enabled(true);
		}
		if (!obj->has_run_begin()) {
			obj->RefreshBegin(true);
		}
		return obj;
	}

	void Hold(T* obj) {
		if (static_cast<int>(m_free.size()) >= size) {
			TOAST_ERROR("Pool::Hold — pool is already full");
			return;
		}
		if (std::ranges::find(m_pool, obj) == m_pool.end()) {
			TOAST_ERROR("Pool::Hold — object does not belong to this pool");
			return;
		}
		obj->enabled(false);
		if constexpr (std::is_base_of_v<toast::Actor, T>) {
			obj->transform()->position({ 99999.0f, 99999.0f, 0.0f });
		}
		m_free.emplace(obj);
	}

private:
	std::array<T*, size> m_pool = {};
	std::stack<T*> m_free;
};
