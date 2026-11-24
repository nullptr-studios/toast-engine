#include "toast/renderer/settings/post_process_profile.hpp"

namespace assets {

PostProcessProfile::PostProcessProfile(const toml::table& table, Handle<Schema> schema) : Data(table, std::move(schema)) { }

void PostProcessProfile::onReloaded() {
	m_version.fetch_add(1, std::memory_order_relaxed);
}

}
