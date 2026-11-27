/**
 * @file post_process_params.hpp
 * @author dario
 * @date 24/11/2026
 *
 * @brief Post-process knobs as data, laid out by core://schemas/post_process.schema.json
 */

#pragma once
#include "post_process_settings.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <toast/assets/data.hpp>
#include <toast/assets/schema.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>
#include <unordered_map>
#include <vector>

namespace renderer {

/**
 * @brief A flat set of named post-process values
 */
class TOAST_API PostProcessParams {
public:
	enum class Kind : uint8_t {
		number,
		integer,        ///< stored as a float
		boolean,
		enumeration,    ///< index into Slot::options
		color,
		asset           ///< UID in ParamValue::asset
	};

	struct ParamValue {
		glm::vec3 v {0.0f};
		toast::UID asset {uint64_t {0}};
	};

	struct Slot {
		std::string name;
		Kind kind {Kind::number};
		float min {-std::numeric_limits<float>::infinity()};
		float max {std::numeric_limits<float>::infinity()};
		ParamValue fallback;
		std::vector<std::string> options;    ///< enumeration only
	};

	/// Immutable and shared between every params object built from the same schema
	struct TOAST_API Layout {
		std::vector<Slot> slots;

		[[nodiscard]]
		auto find(std::string_view name) const -> std::optional<uint32_t>;

		void add(Slot slot);

		/// Walks nested objects
		static auto fromSchema(const assets::Schema& schema) -> std::shared_ptr<const Layout>;

	private:
		struct NameHash {
			using is_transparent = void;

			auto operator()(std::string_view s) const noexcept -> size_t { return std::hash<std::string_view> {}(s); }
		};

		std::unordered_map<std::string, uint32_t, NameHash, std::equal_to<>> m_index;
	};

	static auto coreLayout() -> std::shared_ptr<const Layout>;

	PostProcessParams() : PostProcessParams(coreLayout()) { }

	explicit PostProcessParams(std::shared_ptr<const Layout> layout);

	void reset();

	/// Defaults first
	void loadFrom(const assets::Data& profile);

	void blend(const PostProcessParams& source, float weight);

	auto setNumber(std::string_view name, float value) -> bool;

	auto setColor(std::string_view name, const glm::vec3& value) -> bool;

	auto setOption(std::string_view name, std::string_view option) -> bool;

	[[nodiscard]]
	auto number(std::string_view name) const -> std::optional<float>;

	[[nodiscard]]
	auto color(std::string_view name) const -> std::optional<glm::vec3>;

	[[nodiscard]]
	auto assetUid(std::string_view name) const -> std::optional<toast::UID>;

	[[nodiscard]]
	auto layout() const noexcept -> const std::shared_ptr<const Layout>& {
		return m_layout;
	}

	[[nodiscard]]
	auto resolve() const -> PostProcessSettings;

private:
	std::shared_ptr<const Layout> m_layout;
	std::vector<ParamValue> m_values;
};

}
