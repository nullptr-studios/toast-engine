#include "model.hpp"

#include "fuzzy.hpp"
#include "node_command.hpp"
#include "ranking.hpp"
#include "relative.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <toast/params/registry.hpp>
#include <toast/params/text.hpp>
#include <unordered_set>

namespace toast::console {
namespace {

using namespace toast::params;

constexpr std::string_view k_whitespace = " \t";

auto trim(std::string_view text) -> std::string_view {
	const size_t first = text.find_first_not_of(k_whitespace);
	if (first == std::string_view::npos) {
		return {};
	}
	return text.substr(first, text.find_last_not_of(k_whitespace) - first + 1);
}

auto currentText(const ParamBase& param) -> std::string {
	const std::string path = std::string {param.group()->name()} + "." + std::string {param.id()};
	auto value = Registry::get().pendingValue(path);
	return toText(value ? *value : param.snapshot(), param.type(), param.options());
}

auto pathOf(const ParamBase& param) -> std::string {
	return std::string {param.group()->name()} + "." + std::string {param.id()};
}

auto editDistance(std::string_view a, std::string_view b) -> size_t {
	std::vector<size_t> previous(b.size() + 1);
	std::vector<size_t> current(b.size() + 1);
	for (size_t j = 0; j <= b.size(); ++j) {
		previous[j] = j;
	}
	for (size_t i = 1; i <= a.size(); ++i) {
		current[0] = i;
		for (size_t j = 1; j <= b.size(); ++j) {
			const size_t substitute = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
			current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitute});
		}
		std::swap(previous, current);
	}
	return previous[b.size()];
}

auto closeMatches(std::string_view typed) -> std::vector<std::string> {
	struct Candidate {
		std::string path;
		size_t distance;
	};

	std::vector<Candidate> candidates;
	const size_t allowed = std::max<size_t>(2, typed.size() / 5);
	for (const GroupBase* group : Registry::get().groups()) {
		for (const ParamBase* param : group->params()) {
			std::string path = pathOf(*param);
			const size_t distance = editDistance(typed, path);
			if (distance <= allowed) {
				candidates.push_back({std::move(path), distance});
			}
		}
	}
	std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) {
		return a.distance != b.distance ? a.distance < b.distance : a.path < b.path;
	});
	std::vector<std::string> out;
	for (size_t i = 0; i < candidates.size() && i < 3; ++i) {
		out.push_back(std::move(candidates[i].path));
	}
	return out;
}

auto joinWithOr(const std::vector<std::string>& items) -> std::string {
	std::string out;
	for (size_t i = 0; i < items.size(); ++i) {
		out += i == 0 ? "" : (i + 1 == items.size() ? " or " : ", ");
		out += items[i];
	}
	return out;
}

auto valueNames(const ParamBase& param) -> std::vector<std::string> {
	std::vector<std::string> names;
	if (param.isArray()) {
		return names;
	}
	if (param.type() == Type::boolean) {
		return {"true", "false"};
	}
	if (param.type() == Type::integer) {
		for (const auto option : param.options()) {
			names.emplace_back(option);
		}
	}
	return names;
}

auto completeSet(std::string_view line, size_t cursor) -> Completion {
	cursor = std::min(cursor, line.size());
	Completion completion;

	const size_t word_begin = std::min(line.find_first_not_of(k_whitespace), line.size());
	size_t word_end = line.find_first_of(k_whitespace, word_begin);
	if (word_end == std::string_view::npos) {
		word_end = line.size();
	}

	if (cursor > word_end) {
		const ParamBase* param = Registry::get().find(line.substr(word_begin, word_end - word_begin));
		if (param == nullptr) {
			return completion;
		}
		completion.hint = currentText(*param);
		const size_t argument_begin = std::min(line.find_first_not_of(k_whitespace, word_end), line.size());
		const size_t partial_end = std::max(cursor, argument_begin);
		const std::string_view partial = line.substr(argument_begin, partial_end - argument_begin);
		completion.replace_begin = partial_end == cursor && argument_begin > cursor ? cursor : argument_begin;
		completion.replace_end = partial_end;

		std::vector<Scored> scored;
		for (const auto& name : valueNames(*param)) {
			if (const auto score = fuzzyScore(partial, name)) {
				scored.push_back({
				  {name, name, {}, EntryKind::value},
          *score
				});
			}
		}
		if (!partial.empty()) {
			rank(scored);
		}
		for (auto& item : scored) {
			completion.entries.push_back(std::move(item.entry));
		}
		return completion;
	}

	const std::string_view typed = line.substr(word_begin, cursor - word_begin);
	const size_t last_dot = typed.rfind('.');
	const std::string_view scope = last_dot == std::string_view::npos ? std::string_view {} : typed.substr(0, last_dot);
	const std::string_view partial = last_dot == std::string_view::npos ? typed : typed.substr(last_dot + 1);
	completion.replace_begin = word_begin + (last_dot == std::string_view::npos ? 0 : last_dot + 1);
	completion.replace_end = word_end;

	if (last_dot == std::string_view::npos) {
		std::vector<Scored> groups;
		for (const GroupBase* group : Registry::get().groups()) {
			const std::string name {group->name()};
			if (const auto score = fuzzyScore(partial, name)) {
				groups.push_back({
				  {name, name + ".", {}, EntryKind::group},
          *score
				});
			}
		}
		append(completion.entries, groups);
		return completion;
	}

	const size_t group_dot = scope.find('.');
	const GroupBase* group = Registry::get().findGroup(scope.substr(0, group_dot));
	if (group == nullptr) {
		return completion;
	}
	const std::string inside =
	    group_dot == std::string_view::npos ? std::string {} : std::string {scope.substr(group_dot + 1)} + ".";

	std::vector<Scored> variables;
	std::vector<Scored> subgroups;
	std::unordered_set<std::string> seen;
	for (const ParamBase* param : group->params()) {
		const std::string_view id = param->id();
		if (!id.starts_with(inside)) {
			continue;
		}
		const std::string_view rest = id.substr(inside.size());
		const size_t dot = rest.find('.');
		if (dot == std::string_view::npos) {
			if (const auto score = fuzzyScore(partial, rest)) {
				variables.push_back({
				  {std::string {rest}, std::string {rest}, currentText(*param), EntryKind::variable},
          *score
				});
			}
		} else {
			const std::string name {rest.substr(0, dot)};
			if (const auto score = fuzzyScore(partial, name); score && seen.insert(name).second) {
				subgroups.push_back({
				  {name, name + ".", {}, EntryKind::subgroup},
          *score
				});
			}
		}
	}
	append(completion.entries, variables);
	append(completion.entries, subgroups);
	return completion;
}

}

auto Model::complete(std::string_view line, size_t cursor) const -> Completion {
	cursor = std::min(cursor, line.size());
	Completion completion;

	const size_t first = std::min(line.find_first_not_of(k_whitespace), line.size());
	size_t first_end = line.find_first_of(k_whitespace, first);
	if (first_end == std::string_view::npos) {
		first_end = line.size();
	}

	// the first word is the command
	if (cursor <= first_end) {
		const std::string_view typed = line.substr(first, cursor - first);
		completion.replace_begin = first;
		completion.replace_end = first_end;
		std::vector<Scored> commands;
		for (const std::string_view name : {"set", "node", "show", "hide", "restore"}) {
			if (const auto score = fuzzyScore(typed, name)) {
				commands.push_back({
				  {std::string {name}, name == "restore" ? std::string {name} : std::string {name} + " ", {}, EntryKind::command},
          *score
				});
			}
		}
		append(completion.entries, commands);
		return completion;
	}

	const std::string_view command = line.substr(first, first_end - first);
	if (command == "node" || command == "show" || command == "pin") {
		return completeNode(m_node_root, line, cursor, first_end);
	}
	if (command == "hide" || command == "unpin") {
		return completeHide(line, cursor, first_end);
	}
	if (command == "set") {
		completion = completeSet(line.substr(first_end), cursor - first_end);
		completion.replace_begin += first_end;
		completion.replace_end += first_end;
	}
	return completion;
}

auto Model::execute(std::string_view raw) -> Result {
	const std::string_view line = trim(raw);
	if (line.empty()) {
		return {true, {}};
	}

	const size_t command_end = line.find_first_of(k_whitespace);
	const std::string_view command = line.substr(0, command_end);
	const std::string_view arguments = command_end == std::string_view::npos ? std::string_view {} : trim(line.substr(command_end));

	auto& registry = Registry::get();
	if (command == "restore" && arguments.empty()) {
		registry.requestRestore();
		return {true, "restoring everything from disk"};
	}
	if (command == "node") {
		return executeNode(m_node_root, arguments);
	}
	if (command == "show" || command == "pin") {
		return pin(arguments);
	}
	if (command == "hide" || command == "unpin") {
		return hide(arguments);
	}
	if (command != "set") {
		if (registry.find(command) != nullptr) {
			return {false, std::format("did you mean 'set {}'?", line)};
		}
		return {false, std::format("no command '{}'; there are set, node, show, hide and restore", command)};
	}
	if (arguments.empty()) {
		return {false, "usage: set <path> [value]"};
	}

	const size_t path_end = arguments.find_first_of(k_whitespace);
	const std::string_view path = arguments.substr(0, path_end);
	const std::string_view value_text = path_end == std::string_view::npos ? std::string_view {} : trim(arguments.substr(path_end));

	ParamBase* param = registry.find(path);
	if (param == nullptr && path.starts_with("render.")) {
		// `render.view_mode` for `renderer.view_mode`
		param = registry.find("renderer." + std::string {path.substr(7)});
	}
	if (param == nullptr) {
		std::string message = std::format("no parameter '{}'", path);
		if (const auto close = closeMatches(path); !close.empty()) {
			message += std::format(", did you mean {}?", joinWithOr(close));
		} else if (path.find('.') == std::string_view::npos) {
			message = std::format("no group '{}'", path);
		}
		return {false, std::move(message)};
	}

	if (value_text.empty()) {
		return {true, std::format("{} = {}", path, currentText(*param))};
	}

	std::string resolved;
	std::string_view text = value_text;
	if (hasRelative(text)) {
		if (param->isArray()) {
			return {false, std::format("{}: a list cannot be written relative to itself", path)};
		}
		auto worked_out = resolveRelative(text, currentText(*param), param->type());
		if (!worked_out) {
			return {false, std::format("{}: {}", path, worked_out.error())};
		}
		resolved = std::move(*worked_out);
		text = resolved;
	}

	auto value = fromText(text, param->type(), param->isArray(), param->options());
	if (!value) {
		return {false, std::format("{}: {}", path, value.error())};
	}

	if (param->type() == Type::asset && m_asset_check) {
		const auto check = [&](const Scalar& scalar) {
			const auto* asset = std::get_if<AssetRef>(&scalar);
			return asset == nullptr || asset->uri.empty() || m_asset_check(asset->uri);
		};
		const auto missing = [&]() -> std::string {
			if (const auto* scalar = std::get_if<Scalar>(&*value)) {
				return check(*scalar) ? std::string {} : std::get<AssetRef>(*scalar).uri;
			}
			for (const auto& item : std::get<std::vector<Scalar>>(*value)) {
				if (!check(item)) {
					return std::get<AssetRef>(item).uri;
				}
			}
			return {};
		}();
		if (!missing.empty()) {
			return {false, std::format("{}: no asset '{}'", path, missing)};
		}
	}

	if (!param->accepts(*value)) {
		return {false, std::format("{}: that is not a value it can take", path)};
	}
	const std::string shown = toText(*value, param->type(), param->options());
	registry.request(pathOf(*param), std::move(*value), Layer::runtime);
	return {true, std::format("{} = {}", path, shown)};
}

namespace {

auto lowered(std::string_view text) -> std::string {
	std::string out {text};
	std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

}

auto Model::describe(const Pin& pin) const -> std::pair<std::string, std::string> {
	if (pin.parameter) {
		const ParamBase* param = Registry::get().find(pin.spec);
		return {pin.spec, param != nullptr ? currentText(*param) : std::string {"?"}};
	}
	const Result described = describeNode(m_node_root, pin.spec);
	if (!described.ok) {
		return {pin.spec, std::format("({})", described.message)};
	}
	const size_t split = described.message.find(" = ");
	if (split == std::string::npos) {
		return {pin.spec, described.message};
	}
	return {described.message.substr(0, split), described.message.substr(split + 3)};
}

auto Model::pinnedLines() const -> std::vector<std::string> {
	std::vector<std::string> lines;
	lines.reserve(m_pins.size());
	for (const Pin& pin : m_pins) {
		const auto [label, value] = describe(pin);
		lines.push_back(std::format("{} = {}", label, value));
	}
	return lines;
}

auto Model::pin(std::string_view spec) -> Result {
	spec = trim(spec);
	if (spec.empty()) {
		return {false, "usage: show <node>.<variable>   or   show <parameter>"};
	}
	const std::string wanted = lowered(spec);
	if (std::ranges::any_of(m_pins, [&](const Pin& pin) { return lowered(pin.spec) == wanted; })) {
		return {false, std::format("{} is already shown", spec)};
	}
	if (Registry::get().find(spec) != nullptr) {
		m_pins.push_back({std::string {spec}, true});
		return {true, std::format("showing {}", spec)};
	}
	const Result described = describeNode(m_node_root, spec);
	if (!described.ok) {
		return described;
	}
	m_pins.push_back({std::string {spec}, false});
	return {true, std::format("showing {}", spec)};
}

auto Model::hide(std::string_view spec) -> Result {
	spec = trim(spec);
	if (spec.empty()) {
		if (m_pins.empty()) {
			return {true, "nothing is shown"};
		}
		std::string list;
		for (const Pin& pin : m_pins) {
			list += list.empty() ? "" : ", ";
			list += pin.spec;
		}
		return {true, std::format("shown: {}", list)};
	}
	if (spec == "all" || spec == "*") {
		m_pins.clear();
		return {true, "nothing is shown any more"};
	}
	const std::string wanted = lowered(spec);
	const auto at = std::ranges::find_if(m_pins, [&](const Pin& pin) { return lowered(pin.spec) == wanted; });
	if (at == m_pins.end()) {
		return {false, std::format("{} is not shown", spec)};
	}
	const std::string name = at->spec;
	m_pins.erase(at);
	return {true, std::format("hid {}", name)};
}

auto Model::completeHide(std::string_view line, size_t cursor, size_t base) const -> Completion {
	Completion completion;
	const size_t begin = std::min(line.find_first_not_of(k_whitespace, std::min(base, line.size())), line.size());
	cursor = std::clamp(cursor, begin, line.size());
	const std::string_view partial = line.substr(begin, cursor - begin);
	completion.replace_begin = begin;
	completion.replace_end = line.size();

	std::vector<Scored> scored;
	for (const Pin& pin : m_pins) {
		if (const auto score = fuzzyScore(partial, pin.spec)) {
			scored.push_back({
			  {pin.spec, pin.spec, describe(pin).second, EntryKind::field},
        *score
			});
		}
	}
	if (!partial.empty()) {
		rank(scored);
	}
	for (auto& item : scored) {
		completion.entries.push_back(std::move(item.entry));
	}
	return completion;
}

void Model::setNodeRoot(std::function<Box<Node>()> root) {
	m_node_root = std::move(root);
}

void Model::applyQueued() {
	runQueuedNodeWork();
}

void Model::setAssetCheck(std::function<bool(std::string_view)> check) {
	m_asset_check = std::move(check);
}

}
