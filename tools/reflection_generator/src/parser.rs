use super::*;
use tree_sitter::StreamingIterator;
use std::collections::BTreeMap;

fn has_attribute(attributes: &BTreeMap<String, Vec<String>>, name: &str) -> bool {
	attributes.contains_key(name)
}

fn clean_argument(arg: &str) -> String {
	let trimmed = arg.trim();
	// Remove surrounding quotes if present
	if (trimmed.starts_with('"') && trimmed.ends_with('"')) ||
	   (trimmed.starts_with('\'') && trimmed.ends_with('\'')) {
		trimmed[1..trimmed.len()-1].to_string()
	} else {
		trimmed.to_string()
	}
}

pub fn get_class(node: tree_sitter::Node, source_code: &str) -> Option<Class> {
	let class_name = if let Some(name_node) = node.child_by_field_name("name") {
		source_code[name_node.byte_range()].to_string()
	} else {
		return None;
	};

	// Extract parent class name if it exists
	let mut parent = String::new();
	let query = Query::new(
		&tree_sitter_cpp::LANGUAGE.into(),
		"(base_class_clause(qualified_identifier) @name)"
	).unwrap();
	let mut cursor = QueryCursor::new();
	let mut captures = cursor.captures(&query, node, source_code.as_bytes());

	if let Some((m, _)) = captures.next() {
		parent = source_code[m.captures[0].node.byte_range()].to_string();
	}

	// Get all attributes first to check for ToastNode
	let all_attributes = get_attributes(node, source_code);

	// Only include ToastNode classes
	if !has_attribute(&all_attributes, "ToastNode") {
		return None;
	}

	let class = Class {
		name: class_name,
		parent,
		attributes: get_attributes_skip(node, source_code, &["ToastNode"]),
		functions: get_functions(node, source_code),
		variables: get_variables(node, source_code),
	};

	Some(class)
}

fn get_functions(node: tree_sitter::Node, source_code: &str) -> BTreeMap<String, bool> {
	let mut functions = BTreeMap::new();
	let lifecycle_methods = vec!["preInit", "init", "begin", "earlyTick", "tick", "postPhysics", "lateTick", "end", "destroy", "onEnable", "onDisable"];

	// Initialize all lifecycle methods as false
	for method in &lifecycle_methods {
		functions.insert(method.to_string(), false);
	}

	let query = Query::new(
		&tree_sitter_cpp::LANGUAGE.into(),
		"(field_identifier) @field",
	)
		.unwrap();
	let mut cursor = QueryCursor::new();
	let mut captures = cursor.captures(&query, node, source_code.as_bytes());

	while let Some((m, _)) = captures.next() {
		let field_node = m.captures[0].node;

		if let Some(parent) = field_node.parent()
			&& parent.kind() == "function_declarator"
			&& let Some(_root) = parent.parent()
		{
			let func_name = source_code[field_node.byte_range()].to_string();
			// Only mark as true if it's a tick function
			if functions.contains_key(&func_name) {
				functions.insert(func_name, true);
			}
		}
	}
	functions
}

fn get_variables(node: tree_sitter::Node, source_code: &str) -> BTreeMap<String, VariableInfo> {
	let mut variables = BTreeMap::new();

	let query = Query::new(
		&tree_sitter_cpp::LANGUAGE.into(),
		"(field_identifier) @field",
	)
		.unwrap();
	let mut cursor = QueryCursor::new();
	let mut captures = cursor.captures(&query, node, source_code.as_bytes());

	while let Some((m, _)) = captures.next() {
		let field_node = m.captures[0].node;

		if let Some(parent) = field_node.parent()
			&& parent.kind() == "field_declaration"
		{
			// Skip nested struct definitions
			if let Some(data_type) = parent.child_by_field_name("type")
				&& data_type.kind() == "struct_specifier"
			{
				continue;
			}

			// Get all attributes to check for Serialize
			let all_attributes = get_attributes(parent, source_code);

			// Only include variables with Serialize attribute
			if !has_attribute(&all_attributes, "Serialize") {
				continue;
			}

			let attributes = get_attributes_skip(parent, source_code, &["Serialize"]);

			// Extract type
			let type_name = parent.child_by_field_name("type")
				.map(|t| source_code[t.byte_range()].trim().to_string())
				.unwrap_or_default();

			// Extract default value from initializer if present
			let default = parent.child_by_field_name("default_value")
				.map(|init| source_code[init.byte_range()].trim().to_string());

			let var_name = source_code[field_node.byte_range()].to_string();
			variables.insert(var_name, VariableInfo {
				type_name,
				default,
				attributes,
			});
		}
	}
	variables
}

fn get_attributes(node: tree_sitter::Node, source_code: &str) -> BTreeMap<String, Vec<String>> {
	get_attributes_skip(node, source_code, &[])
}

fn get_attributes_skip(node: tree_sitter::Node, source_code: &str, skip: &[&str]) -> BTreeMap<String, Vec<String>> {
	let mut attributes = BTreeMap::new();

	for list in node.children(&mut node.walk()) {
		if list.kind() != "attribute_declaration" {
			continue;
		}
		for attr_node in list.children(&mut list.walk()) {
			if attr_node.kind() != "attribute" {
				continue;
			}
			let attrib_name = if let Some(name_node) = attr_node.child_by_field_name("name") {
				source_code[name_node.byte_range()].to_string()
			} else {
				continue;
			};

			// Skip this attribute if it's in the skip list
			if skip.contains(&attrib_name.as_str()) {
				continue;
			}

			let mut arguments = Vec::new();

			if let Some(arg_list) = attr_node
				.child_by_field_name("name")
				.and_then(|n| n.next_named_sibling())
			{
				for arg in arg_list.children(&mut arg_list.walk()) {
					if !arg.is_named() {
						continue;
					}
					let arg_text = source_code[arg.byte_range()].to_string();
					arguments.push(clean_argument(&arg_text));
				}
			}

			attributes.insert(attrib_name, arguments);
		}
	}
	attributes
}
