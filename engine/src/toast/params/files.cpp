#include "files.hpp"

#include "registry.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <toast/log.hpp>

namespace toast::params {
namespace {

auto tidy(double value) -> double {
	const auto narrowed = static_cast<float>(value);
	if (static_cast<double>(narrowed) != value) {
		return value;
	}
	std::array<char, 64> buffer {};
	const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), narrowed);
	double shortest = value;
	std::from_chars(buffer.data(), written.ptr, shortest);
	return shortest;
}

auto scalarToJson(const Scalar& scalar) -> Document {
	return std::visit(
	    [](const auto& v) -> Document {
		    using T = std::decay_t<decltype(v)>;
		    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int64_t> || std::is_same_v<T, std::string>) {
			    return v;
		    } else if constexpr (std::is_same_v<T, double>) {
			    return tidy(v);
		    } else if constexpr (std::is_same_v<T, glm::vec2>) {
			    return Document::array({tidy(v.x), tidy(v.y)});
		    } else if constexpr (std::is_same_v<T, glm::vec3>) {
			    return Document::array({tidy(v.x), tidy(v.y), tidy(v.z)});
		    } else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>) {
			    return Document::array({tidy(v.x), tidy(v.y), tidy(v.z), tidy(v.w)});
		    } else {
			    return v.uri;
		    }
	    },
	    scalar
	);
}

auto valueToJson(const Value& value) -> Document {
	if (const auto* scalar = std::get_if<Scalar>(&value)) {
		return scalarToJson(*scalar);
	}
	auto array = Document::array();
	for (const auto& item : std::get<std::vector<Scalar>>(value)) {
		array.push_back(scalarToJson(item));
	}
	return array;
}

auto jsonComponents(const Document& node, size_t count) -> std::optional<std::array<float, 4>> {
	if (!node.is_array() || node.size() != count) {
		return std::nullopt;
	}
	std::array<float, 4> out {};
	for (size_t i = 0; i < count; ++i) {
		if (!node[i].is_number()) {
			return std::nullopt;
		}
		out[i] = node[i].get<float>();
	}
	return out;
}

auto jsonToScalar(const Document& node, Type type) -> std::optional<Scalar> {
	switch (type) {
		case Type::boolean: return node.is_boolean() ? std::optional<Scalar> {node.get<bool>()} : std::nullopt;
		case Type::integer: return node.is_number_integer() ? std::optional<Scalar> {node.get<int64_t>()} : std::nullopt;
		case Type::floating: return node.is_number() ? std::optional<Scalar> {node.get<double>()} : std::nullopt;
		case Type::string: return node.is_string() ? std::optional<Scalar> {node.get<std::string>()} : std::nullopt;
		case Type::asset: return node.is_string() ? std::optional<Scalar> {AssetRef {node.get<std::string>()}} : std::nullopt;
		case Type::vec2: {
			const auto c = jsonComponents(node, 2);
			return c ? std::optional<Scalar> {glm::vec2 {(*c)[0], (*c)[1]}} : std::nullopt;
		}
		case Type::vec3: {
			const auto c = jsonComponents(node, 3);
			return c ? std::optional<Scalar> {glm::vec3 {(*c)[0], (*c)[1], (*c)[2]}} : std::nullopt;
		}
		case Type::vec4: {
			const auto c = jsonComponents(node, 4);
			return c ? std::optional<Scalar> {glm::vec4 {(*c)[0], (*c)[1], (*c)[2], (*c)[3]}} : std::nullopt;
		}
		case Type::quat: {
			const auto c = jsonComponents(node, 4);
			return c ? std::optional<Scalar> {glm::quat {(*c)[3], (*c)[0], (*c)[1], (*c)[2]}} : std::nullopt;
		}
	}
	return std::nullopt;
}

auto jsonToValue(const Document& node, Type type, bool is_array) -> std::optional<Value> {
	if (!is_array) {
		auto scalar = jsonToScalar(node, type);
		return scalar ? std::optional<Value> {Value {std::move(*scalar)}} : std::nullopt;
	}
	if (!node.is_array()) {
		return std::nullopt;
	}
	std::vector<Scalar> items;
	items.reserve(node.size());
	for (const auto& element : node) {
		auto scalar = jsonToScalar(element, type);
		if (!scalar) {
			return std::nullopt;
		}
		items.push_back(std::move(*scalar));
	}
	return Value {std::move(items)};
}

auto splitId(std::string_view id) -> std::vector<std::string> {
	std::vector<std::string> parts;
	size_t start = 0;
	while (true) {
		const size_t dot = id.find('.', start);
		if (dot == std::string_view::npos) {
			parts.emplace_back(id.substr(start));
			return parts;
		}
		parts.emplace_back(id.substr(start, dot - start));
		start = dot + 1;
	}
}

auto findNode(const Document& document, std::string_view id) -> const Document* {
	const Document* current = &document;
	for (const auto& part : splitId(id)) {
		if (!current->is_object()) {
			return nullptr;
		}
		const auto it = current->find(part);
		if (it == current->end()) {
			return nullptr;
		}
		current = &*it;
	}
	return current;
}

void setNode(Document& document, std::string_view id, Document value) {
	Document* current = &document;
	const auto parts = splitId(id);
	for (size_t i = 0; i + 1 < parts.size(); ++i) {
		Document& child = (*current)[parts[i]];
		if (!child.is_object()) {
			child = Document::object();
		}
		current = &child;
	}
	(*current)[parts.back()] = std::move(value);
}

void eraseNode(Document& document, std::string_view id) {
	const auto parts = splitId(id);
	std::vector<Document*> chain {&document};
	for (size_t i = 0; i + 1 < parts.size(); ++i) {
		Document* current = chain.back();
		const auto it = current->find(parts[i]);
		if (!current->is_object() || it == current->end() || !it->is_object()) {
			return;
		}
		chain.push_back(&*it);
	}
	chain.back()->erase(parts.back());
	for (size_t i = chain.size() - 1; i > 0; --i) {
		if (!chain[i]->empty()) {
			break;
		}
		chain[i - 1]->erase(parts[i - 1]);
	}
}

}

static auto bsonToResult(const std::string& bytes) -> ReadResult {
	try {
		auto document = Document::from_bson(std::string_view {bytes});
		if (!document.is_object()) {
			return {ReadStatus::corrupt, Document::object(), "not a document"};
		}
		return {ReadStatus::ok, std::move(document), {}};
	} catch (const std::exception& error) { return {ReadStatus::corrupt, Document::object(), error.what()}; }
}

namespace {

auto readAll(const std::filesystem::path& path) -> std::string {
	std::ifstream in(path, std::ios::binary);
	std::stringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

}

DirectoryConfigSource::DirectoryConfigSource(std::filesystem::path directory, ConfigFormat format)
    : m_directory(std::move(directory)),
      m_format(format) { }

auto DirectoryConfigSource::pathFor(std::string_view group) const -> std::filesystem::path {
	return m_directory / (std::string {group} + (m_format == ConfigFormat::toml ? ".toml" : ".bson"));
}

auto DirectoryConfigSource::read(std::string_view group) const -> ReadResult {
	const auto path = pathFor(group);
	std::error_code ec;
	if (!std::filesystem::is_regular_file(path, ec)) {
		return {};
	}
	if (m_format == ConfigFormat::bson) {
		return bsonToResult(readAll(path));
	}
	auto parsed = tomlToDocument(readAll(path));
	if (!parsed) {
		return {ReadStatus::corrupt, Document::object(), parsed.error()};
	}
	return {ReadStatus::ok, std::move(*parsed), {}};
}

auto DirectoryConfigSource::write(std::string_view group, const Document& document) -> bool {
	const auto path = pathFor(group);
	std::error_code ec;
	std::filesystem::create_directories(m_directory, ec);

	if (read(group).status == ReadStatus::corrupt) {
		auto backup = path;
		backup += ".bak";
		std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
		TOAST_WARN("Params", "{} could not be read; kept as {}", path.string(), backup.string());
	}

	auto temporary = path;
	temporary += ".tmp";
	{
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		if (!out) {
			return false;
		}
		if (m_format == ConfigFormat::bson) {
			const auto bytes = Document::to_bson(document);
			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		} else {
			out << documentToToml(document);
		}
		if (!out.good()) {
			return false;
		}
	}
	std::filesystem::rename(temporary, path, ec);
	return !ec;
}

Files::Files(std::unique_ptr<ConfigSource> project, std::unique_ptr<ConfigSource> saved)
    : m_project(std::move(project)),
      m_saved(std::move(saved)) { }

auto Files::source(Layer layer) const -> ConfigSource* {
	switch (layer) {
		case Layer::project: return m_project.get();
		case Layer::saved: return m_saved.get();
		default: return nullptr;
	}
}

void Files::load(GroupBase& group, bool keep_unsaved) {
	for (const Layer layer : {Layer::project, Layer::saved}) {
		ConfigSource* src = source(layer);
		if (src == nullptr || (keep_unsaved && group.dirty(layer))) {
			continue;
		}

		auto result = src->read(group.name());
		if (result.status == ReadStatus::corrupt) {
			TOAST_ERROR("Params", "'{}' could not be read ({}); using defaults for it", group.name(), result.error);
		}
		Document document =
		    result.status == ReadStatus::ok && result.document.is_object() ? std::move(result.document) : Document::object();

		for (ParamBase* param : group.params()) {
			if (layer == Layer::project && param->kind() == Kind::user) {
				continue;
			}
			const Document* node = findNode(document, param->id());
			if (node == nullptr) {
				if (param->layerValue(layer).has_value()) {
					param->setLayer(layer, std::nullopt);
				}
				continue;
			}
			auto value = jsonToValue(*node, param->type(), param->isArray());
			if (!value || !param->accepts(*value)) {
				if (m_reported.insert(std::string {group.name()} + "." + std::string {param->id()}).second) {
					TOAST_WARN(
					    "Params",
					    "'{}.{}' in the {} file is not a valid value; keeping the default",
					    group.name(),
					    param->id(),
					    layer == Layer::project ? "project" : "saved"
					);
				}
				param->setLayer(layer, std::nullopt);
				continue;
			}
			param->setLayer(layer, std::move(value));
		}

		m_documents[{std::string {group.name()}, layer}] = std::move(document);
		group.clearDirty(layer);
	}
}

auto Files::save(GroupBase& group, Layer layer) -> bool {
	ConfigSource* src = source(layer);
	if (src == nullptr) {
		return false;
	}

	const auto key = std::make_pair(std::string {group.name()}, layer);
	Document document = Document::object();
	if (const auto it = m_documents.find(key); it != m_documents.end()) {
		document = it->second;
	}

	for (ParamBase* param : group.params()) {
		const bool is_user = param->kind() == Kind::user;
		if (layer == Layer::project && is_user) {
			continue;
		}

		const auto& stored = param->layerValue(layer);
		const Document* existing = findNode(document, param->id());

		Document below = valueToJson(param->defaultValue());
		if (layer == Layer::saved && !is_user) {
			if (const auto& project_value = param->layerValue(Layer::project); project_value && param->accepts(*project_value)) {
				below = valueToJson(*project_value);
			}
		}

		if (stored.has_value() && param->accepts(*stored)) {
			Document written = valueToJson(*stored);
			if (written != below) {
				setNode(document, param->id(), std::move(written));
			} else if (existing != nullptr) {
				eraseNode(document, param->id());
			}
		} else if (!stored.has_value() && existing != nullptr && jsonToValue(*existing, param->type(), param->isArray())) {
			eraseNode(document, param->id());
		}
	}

	if (!src->write(group.name(), document)) {
		TOAST_ERROR("Params", "Could not write the {} file of '{}'", layer == Layer::project ? "project" : "saved", group.name());
		return false;
	}
	m_documents[key] = std::move(document);
	group.clearDirty(layer);
	return true;
}

auto Files::saveDirty() -> size_t {
	size_t written = 0;
	for (const auto& entry : Registry::get().dirtyGroups()) {
		if (save(*entry.group, entry.layer)) {
			++written;
		}
	}
	return written;
}

void Files::reload() {
	Registry::get().clearRuntime();
	for (GroupBase* group : Registry::get().groups()) {
		load(*group, true);
	}
}

}
