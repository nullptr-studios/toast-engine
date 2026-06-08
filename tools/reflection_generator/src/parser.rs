use tree_sitter::{Parser, Query, QueryCursor, StreamingIterator};
use serde::Serialize;

#[derive(Serialize, Clone)]
pub struct Attribute {
	pub name: String,
	pub args: Vec<String>,
}

pub struct Parent {
	pub name: String,
	pub namespace: Option<String>,
}

pub struct Field {
	pub name: String,
	pub type_name: String,
	pub default: Option<String>,
	pub attributes: Vec<Attribute>,
}

pub struct Class {
	pub name: String,
	pub namespace: Option<String>,
	pub parent: Option<Parent>,
	pub attributes: Vec<Attribute>,
	pub functions: Vec<String>,
	pub fields: Vec<Field>,
}

pub fn parse(source: &str) -> Vec<Class> {
	let mut parser = Parser::new();
	parser.set_language(&tree_sitter_cpp::LANGUAGE.into()).expect("Language error");

	let tree = parser.parse(source, None).unwrap();
	let query = Query::new(&tree_sitter_cpp::LANGUAGE.into(), "(class_specifier) @class").unwrap();

	let mut cursor = QueryCursor::new();
	let mut captures = cursor.captures(&query, tree.root_node(), source.as_bytes());

	let mut classes = Vec::new();
	while let Some((m, idx)) = captures.next() {
		let node = m.captures[*idx].node;
		if let Some(class) = get_class(node, source) {
			classes.push(class);
		}
	}
	classes
}

fn get_class(node: tree_sitter::Node, source: &str) -> Option<Class> {
	let name = node.child_by_field_name("name")
		.map(|n| source[n.byte_range()].to_string())?;

	let all_attrs = get_attributes(node, source);
	if !all_attrs.iter().any(|a| a.name == "ToastNode") {
		return None;
	}
	let attributes = all_attrs.into_iter().filter(|a| a.name != "ToastNode").collect();

	Some(Class {
		name,
		namespace: get_namespace(node, source),
		parent: get_parent(node, source),
		attributes,
		functions: get_functions(node, source),
		fields: get_fields(node, source),
	})
}

fn get_namespace(node: tree_sitter::Node, source: &str) -> Option<String> {
	let mut current = node.parent();
	while let Some(p) = current {
		if p.kind() == "namespace_definition" {
			if let Some(name_node) = p.child_by_field_name("name") {
				return Some(source[name_node.byte_range()].to_string());
			}
		}
		current = p.parent();
	}
	None
}

fn get_parent(node: tree_sitter::Node, source: &str) -> Option<Parent> {
	for child in node.children(&mut node.walk()) {
		if child.kind() != "base_class_clause" {
			continue;
		}
		for inner in child.children(&mut child.walk()) {
			match inner.kind() {
				"qualified_identifier" => {
					let full = &source[inner.byte_range()];
					return if let Some(pos) = full.rfind("::") {
						Some(Parent {
							name: full[pos + 2..].to_string(),
							namespace: Some(full[..pos].to_string()),
						})
					} else {
						Some(Parent { name: full.to_string(), namespace: None })
					};
				}
				"type_identifier" => {
					return Some(Parent {
						name: source[inner.byte_range()].to_string(),
						namespace: None,
					});
				}
				_ => {}
			}
		}
	}
	None
}

fn get_functions(node: tree_sitter::Node, source: &str) -> Vec<String> {
	const TICK_FUNCTIONS: &[&str] = &[
		"preInit", "init", "begin", "earlyTick", "tick", "postPhysics",
		"lateTick", "end", "destroy", "onEnable", "onDisable", "load", "save",
	];

	let query = Query::new(&tree_sitter_cpp::LANGUAGE.into(), "(field_identifier) @field").unwrap();
	let mut cursor = QueryCursor::new();
	let mut captures = cursor.captures(&query, node, source.as_bytes());

	let mut functions = Vec::new();
	while let Some((m, _)) = captures.next() {
		let field_node = m.captures[0].node;
		if let Some(parent) = field_node.parent()
			&& parent.kind() == "function_declarator"
			&& parent.parent().is_some()
		{
			let name = source[field_node.byte_range()].to_string();
			if TICK_FUNCTIONS.contains(&name.as_str()) && !functions.contains(&name) {
				functions.push(name);
			}
		}
	}
	functions
}

fn get_fields(node: tree_sitter::Node, source: &str) -> Vec<Field> {
	let query = Query::new(&tree_sitter_cpp::LANGUAGE.into(), "(field_identifier) @field").unwrap();
	let mut cursor = QueryCursor::new();
	let mut captures = cursor.captures(&query, node, source.as_bytes());

	let mut fields = Vec::new();
	while let Some((m, _)) = captures.next() {
		let field_node = m.captures[0].node;
		let Some(parent) = field_node.parent() else { continue };
		if parent.kind() != "field_declaration" {
			continue;
		}

		// Skip nested struct definitions
		if let Some(ty) = parent.child_by_field_name("type")
			&& ty.kind() == "struct_specifier"
		{
			continue;
		}

		let all_attrs = get_attributes(parent, source);
		if !all_attrs.iter().any(|a| a.name == "Serialize") {
			continue;
		}
		let attributes = all_attrs.into_iter().filter(|a| a.name != "Serialize").collect();

		let type_name = parent.child_by_field_name("type")
			.map(|t| source[t.byte_range()].trim().to_string())
			.unwrap_or_default();

		let default = parent.child_by_field_name("default_value")
			.map(|d| source[d.byte_range()].trim().to_string());

		fields.push(Field {
			name: source[field_node.byte_range()].to_string(),
			type_name,
			default,
			attributes,
		});
	}
	fields
}

fn get_attributes(node: tree_sitter::Node, source: &str) -> Vec<Attribute> {
	let mut attributes = Vec::new();

	for list in node.children(&mut node.walk()) {
		if list.kind() != "attribute_declaration" {
			continue;
		}
		for attr_node in list.children(&mut list.walk()) {
			if attr_node.kind() != "attribute" {
				continue;
			}
			let Some(name_node) = attr_node.child_by_field_name("name") else { continue };
			let name = source[name_node.byte_range()].to_string();

			let mut args = Vec::new();
			if let Some(arg_list) = name_node.next_named_sibling() {
				for arg in arg_list.children(&mut arg_list.walk()) {
					if !arg.is_named() {
						continue;
					}
					args.push(clean_argument(&source[arg.byte_range()]));
				}
			}

			attributes.push(Attribute { name, args });
		}
	}
	attributes
}

fn clean_argument(arg: &str) -> String {
	let trimmed = arg.trim();
	if (trimmed.starts_with('"') && trimmed.ends_with('"'))
		|| (trimmed.starts_with('\'') && trimmed.ends_with('\''))
	{
		trimmed[1..trimmed.len() - 1].to_string()
	} else {
		trimmed.to_string()
	}
}
