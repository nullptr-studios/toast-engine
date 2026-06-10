use crate::{Class, Field, Attribute};
use serde::Serialize;
use serde_json::{to_value, Value as json_t};
use minijinja::Environment;
use std::path::Path;
use std::fs;

#[derive(Serialize)]
pub struct NodeInfo {
	pub name:        String,
	pub namespace:   Option<String>,
	pub parent:      Option<ParentInfo>,
	pub attributes:  json_t,
	pub functions:   TickFunctions,
	pub groups:      Vec<GroupInfo>,
	pub global_fields: Vec<FieldInfo>,
	/// Path relative to --include-root
	pub source_file: String,
}

#[derive(Serialize)]
pub struct ParentInfo {
	pub name:      String,
	pub namespace: Option<String>,
}

#[derive(Serialize)]
pub struct TickFunctions {
	pub pre_init:     bool,
	pub init:         bool,
	pub begin:        bool,
	pub early_tick:   bool,
	pub tick:         bool,
	pub post_physics: bool,
	pub late_tick:    bool,
	pub end:          bool,
	pub destroy:      bool,
	pub on_enable:    bool,
	pub on_disable:   bool,
	pub load:         bool,
	pub save:         bool,
}

#[derive(Serialize)]
pub struct GroupInfo {
	pub name:      String,
	pub subgroups: Vec<SubgroupInfo>,
	pub fields:    Vec<FieldInfo>,
}

#[derive(Serialize)]
pub struct SubgroupInfo {
	pub name:   String,
	pub fields: Vec<FieldInfo>,
}

#[derive(Serialize)]
pub struct FieldInfo {
	pub name:       String,
	pub typename:   String,
	pub field_type: FieldType,
	pub is_array:   bool,
	pub attributes: json_t,
	pub default:    Option<String>,
}

#[derive(Serialize)]
pub enum FieldType {
	#[serde(rename = "bool_t")]        Bool,
	#[serde(rename = "int_t")]         Int,
	#[serde(rename = "float_t")]       Float,
	#[serde(rename = "string_t")]      String,
	#[serde(rename = "double_t")]      Double,
	#[serde(rename = "uid_t")]        Uid,
	#[serde(rename = "vec2_t")]        Vec2,
	#[serde(rename = "vec3_t")]        Vec3,
	#[serde(rename = "vec4_t")]        Vec4,
	#[serde(rename = "quaternion_t")]  Quaternion,
}

fn infer_field_type(type_name: &str) -> FieldType {
	let base = type_name.trim()
		.trim_start_matches("toast::")
		.trim_start_matches("std::")
		.trim_start_matches("glm::");

	if base.starts_with("Box<") { return FieldType::Uid; }

	match base {
		"bool"                                                         => FieldType::Bool,
		"int"  | "int8_t"  | "int16_t"  | "int32_t"  | "int64_t"
		       | "uint8_t" | "uint16_t" | "uint32_t" | "uint64_t"    => FieldType::Int,
		"float"                                                        => FieldType::Float,
		"string"                                                       => FieldType::String,
		"double"                                                       => FieldType::Double,
		"UID"                                                         => FieldType::Uid,
		"vec2"                                                         => FieldType::Vec2,
		"vec3"                                                         => FieldType::Vec3,
		"vec4"                                                         => FieldType::Vec4,
		"quat" | "quaternion"                                          => FieldType::Quaternion,
		_                                                              => FieldType::Int,
	}
}

fn attrs_to_json(attrs: &[Attribute]) -> json_t {
	let map: serde_json::Map<std::string::String, json_t> = attrs.iter()
		.map(|a| (a.name.clone(), serde_json::json!(a.args)))
		.collect();
	json_t::Object(map)
}

fn attr_arg(attrs: &[Attribute], name: &str) -> Option<std::string::String> {
	attrs.iter().find(|a| a.name == name).and_then(|a| a.args.first().cloned())
}

fn build_field(field: &Field) -> FieldInfo {
	let attrs: Vec<_> = field.attributes.iter()
		.filter(|a| a.name != "Group" && a.name != "Subgroup")
		.cloned()
		.collect();

	FieldInfo {
		name:       field.name.clone(),
		typename:   field.type_name.clone(),
		field_type: infer_field_type(&field.type_name),
		is_array:   field.type_name.contains("vector<"),
		attributes: attrs_to_json(&attrs),
		default:    field.default.clone(),
	}
}

pub fn build_node(class: &Class, source_file: &str) -> NodeInfo {
	let fns = &class.functions;
	let functions = TickFunctions {
		pre_init:     fns.contains(&"preInit".to_string()),
		init:         fns.contains(&"init".to_string()),
		begin:        fns.contains(&"begin".to_string()),
		early_tick:   fns.contains(&"earlyTick".to_string()),
		tick:         fns.contains(&"tick".to_string()),
		post_physics: fns.contains(&"postPhysics".to_string()),
		late_tick:    fns.contains(&"lateTick".to_string()),
		end:          fns.contains(&"end".to_string()),
		destroy:      fns.contains(&"destroy".to_string()),
		on_enable:    fns.contains(&"onEnable".to_string()),
		on_disable:   fns.contains(&"onDisable".to_string()),
		load:         fns.contains(&"load".to_string()),
		save:         fns.contains(&"save".to_string()),
	};

	// Bucket fields by Group
	let mut global_fields: Vec<FieldInfo> = Vec::new();
	let mut group_map: std::collections::BTreeMap<
		std::string::String,
		(Vec<FieldInfo>, std::collections::BTreeMap<std::string::String, Vec<FieldInfo>>),
	> = std::collections::BTreeMap::new();

	for field in &class.fields {
		let group    = attr_arg(&field.attributes, "Group");
		let subgroup = attr_arg(&field.attributes, "Subgroup");
		let fi       = build_field(field);

		match (group, subgroup) {
			(Some(g), Some(sg)) => { group_map.entry(g).or_default().1.entry(sg).or_default().push(fi); }
			(Some(g), None)     => { group_map.entry(g).or_default().0.push(fi); }
			_                   => { global_fields.push(fi); }
		}
	}

	let groups = group_map.into_iter().map(|(gname, (fields, subs))| {
		GroupInfo {
			name: gname,
			fields,
			subgroups: subs.into_iter()
				.map(|(sgname, fields)| SubgroupInfo { name: sgname, fields })
				.collect(),
		}
	}).collect();

	NodeInfo {
		name:         class.name.clone(),
		namespace:    class.namespace.clone(),
		parent:       class.parent.as_ref().map(|p| ParentInfo { name: p.name.clone(), namespace: p.namespace.clone() }),
		attributes:   attrs_to_json(&class.attributes),
		functions,
		groups,
		global_fields,
		source_file:  source_file.to_string(),
	}
}

pub fn generate_json(nodes: &[NodeInfo]) -> json_t {
	to_value(nodes).unwrap_or(json_t::Array(vec![]))
}

fn build_template_context(node: &NodeInfo) -> json_t {
	let snake_name = node.name.to_lowercase();

	let qualified_name = match &node.namespace {
		Some(ns) => format!("{}::{}", ns, node.name),
		None     => node.name.clone(),
	};

	let parent_qualified_name: Option<std::string::String> = node.parent.as_ref().map(|p| {
		match &p.namespace {
			Some(ns) => format!("{}::{}", ns, p.name),
			None     => p.name.clone(),
		}
	});

	// Build the flat field array in order
	let mut all_fields_flat: Vec<json_t>  = Vec::new();
	let mut global_field_indices: Vec<usize> = Vec::new();

	let augment_field = |f: &FieldInfo, idx: usize| -> json_t {
		// display_name: Name attr arg[0], else strip m_ prefix
		let display_name = f.attributes.get("Name")
			.and_then(|v| v.as_array())
			.and_then(|a| a.first())
			.and_then(|v| v.as_str())
			.map(|s| s.to_string())
			.unwrap_or_else(|| {
				f.name.strip_prefix("m_").unwrap_or(&f.name).to_string()
			});

		// attrs_list: remaining attributes excluding Group / Subgroup / Name
		let attrs_list: Vec<json_t> = if let json_t::Object(map) = &f.attributes {
			map.iter()
				.filter(|(k, _)| k.as_str() != "Name")
				.map(|(k, v)| serde_json::json!({ "name": k, "args": v }))
				.collect()
		} else {
			vec![]
		};

		serde_json::json!({
			"index":        idx,
			"name":         f.name,
			"member_name":  f.name,
			"display_name": display_name,
			"typename":     f.typename,
			"field_type":   to_value(&f.field_type).unwrap(),
			"is_array":     f.is_array,
			"attributes":   f.attributes,
			"attrs_list":   attrs_list,
			"default":      f.default,
		})
	};

	for f in &node.global_fields {
		let idx = all_fields_flat.len();
		global_field_indices.push(idx);
		all_fields_flat.push(augment_field(f, idx));
	}

	// Augment groups
	let augmented_groups: Vec<json_t> = node.groups.iter().map(|g| {
		let gsnake = to_snake(&g.name);
		let mut field_indices: Vec<usize> = Vec::new();

		for f in &g.fields {
			let idx = all_fields_flat.len();
			field_indices.push(idx);
			all_fields_flat.push(augment_field(f, idx));
		}

		let augmented_subgroups: Vec<json_t> = g.subgroups.iter().map(|sg| {
			let sgsnake = to_snake(&sg.name);
			let mut sg_field_indices: Vec<usize> = Vec::new();

			for f in &sg.fields {
				let idx = all_fields_flat.len();
				sg_field_indices.push(idx);
				all_fields_flat.push(augment_field(f, idx));
			}

			serde_json::json!({
				"name":          sg.name,
				"snake_name":    sgsnake,
				"field_indices": sg_field_indices,
			})
		}).collect();

		serde_json::json!({
			"name":          g.name,
			"snake_name":    gsnake,
			"field_indices": field_indices,
			"subgroups":     augmented_subgroups,
		})
	}).collect();

	// active_tick_fns
	const TICK_MAP: &[(&str, &str)] = &[
		("pre_init",     "preInit"),
		("init",         "init"),
		("begin",        "begin"),
		("early_tick",   "earlyTick"),
		("tick",         "tick"),
		("post_physics", "postPhysics"),
		("late_tick",    "lateTick"),
		("end",          "end"),
		("destroy",      "destroy"),
		("on_enable",    "onEnable"),
		("on_disable",   "onDisable"),
		("load",         "load"),
		("save",         "save"),
	];

	let tf = &node.functions;
	let active_tick_fns: Vec<json_t> = TICK_MAP.iter()
		.filter(|(flag, _)| match *flag {
			"pre_init"     => tf.pre_init,
			"init"         => tf.init,
			"begin"        => tf.begin,
			"early_tick"   => tf.early_tick,
			"tick"         => tf.tick,
			"post_physics" => tf.post_physics,
			"late_tick"    => tf.late_tick,
			"end"          => tf.end,
			"destroy"      => tf.destroy,
			"on_enable"    => tf.on_enable,
			"on_disable"   => tf.on_disable,
			"load"         => tf.load,
			"save"         => tf.save,
			_              => false,
		})
		.map(|(flag, method)| serde_json::json!({ "flag": flag, "method": method }))
		.collect();

	serde_json::json!({
		"name":                  node.name,
		"namespace":             node.namespace,
		"snake_name":            snake_name,
		"qualified_name":        qualified_name,
		"parent_qualified_name": parent_qualified_name,
		"source_file":           node.source_file,
		"attributes":            node.attributes,
		"all_fields_flat":       all_fields_flat,
		"global_field_indices":  global_field_indices,
		"groups":                augmented_groups,
		"active_tick_fns":       active_tick_fns,
	})
}

fn to_snake(s: &str) -> std::string::String {
	s.to_lowercase().replace(' ', "_")
}

pub fn generate_files(nodes: &[NodeInfo], output: &Path, register_fn: &str) {
	// Templates live next to the executable: <exe_dir>/templates/
	let exe_dir = std::env::current_exe()
		.expect("cannot locate executable")
		.parent()
		.expect("exe has no parent dir")
		.to_path_buf();
	let template_dir = exe_dir.join("templates");

	let mut env = Environment::new();
	env.set_loader(minijinja::path_loader(&template_dir));

	// Per-class .generated.hpp
	let node_tmpl = env.get_template("node.generated.hpp.jinja2")
		.unwrap_or_else(|e| panic!("cannot load node.generated.hpp.jinja2: {e}"));

	for node in nodes {
		let ctx  = build_template_context(node);
		let out  = output.join(format!("{}.generated.hpp", ctx["snake_name"].as_str().unwrap()));
		let text = node_tmpl.render(&ctx)
			.unwrap_or_else(|e| panic!("template error for {}: {e}", node.name));
		fs::write(&out, text)
			.unwrap_or_else(|e| panic!("cannot write {}: {e}", out.display()));
	}

	// reflect.generated.cpp
	let cpp_tmpl = env.get_template("reflect.generated.cpp.jinja2")
		.unwrap_or_else(|e| panic!("cannot load reflect.generated.cpp.jinja2: {e}"));

	let all_ctx: Vec<json_t> = nodes.iter().map(build_template_context).collect();
	let cpp_ctx = serde_json::json!({
		"nodes":       all_ctx,
		"register_fn": register_fn,
	});

	let out  = output.join("reflect.generated.cpp");
	let text = cpp_tmpl.render(&cpp_ctx)
		.unwrap_or_else(|e| panic!("template error for reflect.generated.cpp: {e}"));
	fs::write(&out, text)
		.unwrap_or_else(|e| panic!("cannot write {}: {e}", out.display()));
}
