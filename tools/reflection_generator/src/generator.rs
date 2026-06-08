use crate::{Class, Field, Attribute};
use serde::Serialize;
use serde_json::{to_value, Value as json_t};
use minijinja::Environment;

#[derive(Serialize)]
pub struct NodeInfo {
	name: String,
	namespace: Option<String>,
	parent: Option<ParentInfo>,
	attributes: json_t,
	functions: TickFunctions,
	groups: Vec<GroupInfo>,
	global_fields: Vec<FieldInfo>,
}

#[derive(Serialize)]
pub struct ParentInfo {
	name: String,
	namespace: Option<String>,
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
	name: String,
	subgroups: Vec<SubgroupInfo>,
	fields: Vec<FieldInfo>,
}

#[derive(Serialize)]
pub struct SubgroupInfo {
	name: String,
	fields: Vec<FieldInfo>,
}

#[derive(Serialize)]
pub struct FieldInfo {
	name: String,
	typename: String,
	field_type: FieldType,
	is_array: bool,
	attributes: json_t,
	default: Option<String>,
}

#[derive(Serialize)]
pub enum FieldType {
	#[serde(rename = "bool_t")]        Bool,
	#[serde(rename = "int_t")]         Int,
	#[serde(rename = "float_t")]       Float,
	#[serde(rename = "string_t")]      String,
	#[serde(rename = "double_t")]      Double,
	#[serde(rename = "uuid_t")]        Uid,
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
		"UUID"                                                         => FieldType::Uid,
		"vec2"                                                         => FieldType::Vec2,
		"vec3"                                                         => FieldType::Vec3,
		"vec4"                                                         => FieldType::Vec4,
		"quat" | "quaternion"                                          => FieldType::Quaternion,
		_                                                              => FieldType::Int,
	}
}

fn attrs_to_json(attrs: &[Attribute]) -> json_t {
	let map: serde_json::Map<String, json_t> = attrs.iter()
		.map(|a| (a.name.clone(), serde_json::json!(a.args)))
		.collect();
	json_t::Object(map)
}

fn attr_arg(attrs: &[Attribute], name: &str) -> Option<String> {
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

fn build_node(class: &Class) -> NodeInfo {
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

	// Bucket fields by Group / Subgroup
	let mut global_fields: Vec<FieldInfo> = Vec::new();
	let mut group_map: std::collections::BTreeMap<String, (Vec<FieldInfo>, std::collections::BTreeMap<String, Vec<FieldInfo>>)> = std::collections::BTreeMap::new();

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
	}
}

pub fn generate_json(classes: &[Class]) -> json_t {
	let nodes: Vec<NodeInfo> = classes.iter().map(build_node).collect();
	to_value(&nodes).unwrap_or(json_t::Array(vec![]))
}

pub fn generate_files(classes: &[Class]) {
	let env = Environment::new();
}
