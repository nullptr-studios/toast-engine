/// @file Pool.hpp
/// @author Xein
/// @date 16 Feb 2026

#pragma once
#include "Objects/Actor.hpp"
#include "Objects/Scene.hpp"

template<typename T>
concept is_object = std::is_base_of_v<toast::Object, T>;

template<is_object T, int size>
class Pool {
public:
	Pool(toast::Scene* scene) : scene(scene) {
		for (int i = 0; i < size; i++) {
			object_pool[i] = scene->children.Add<T>();
			object_pool[i]->enabled(false);
			object_pool[i]->SetSerialize(false);
			free_objects.emplace(object_pool[i]);
		}
	}

	~Pool() {
		RemoveAll();
	}

	auto Release() -> T* {
		if (free_objects.empty()) {
			TOAST_ERROR("There aren't any free objects in the pool");
			return nullptr;
		}

		auto* obj = free_objects.top();
		obj->enabled(true);
		free_objects.pop();
		return obj;
	}

	void Hold(T* obj) {
		if (free_objects.size() == size) {
			TOAST_ERROR("Pool is full");
			return;
		}

		if (std::ranges::find(object_pool, obj) == object_pool.end()) {
			return;
		}
		obj->enabled(false);
		if constexpr (std::is_base_of_v<toast::Actor, T>) {
			obj->transform()->position({ 0.0f, 0.0f, 0.0f });
		}
		free_objects.emplace(obj);
	}

private:
	void RemoveAll() {
		for (int i = 0; i < size; i++) {
			// scene->children.Remove(object_pool[i]->id());
			object_pool[i] = nullptr;
		}
	}

	toast::Scene* scene;
	std::array<T*, size> object_pool;
	std::stack<T*> free_objects;
};
