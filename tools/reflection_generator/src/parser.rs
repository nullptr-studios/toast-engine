use super::*;

// TODO: anon structs should inherit attributes i guess

pub fn get_class(node: tree_sitter::Node, source_code: &str) -> Option<Class> {
    let class_name = if let Some(name_node) = node.child_by_field_name("name") {
        source_code[name_node.byte_range()].to_string()
    } else {
        return None; // we dont need to reflect anon classes
    };

    let mut class = Class {
        name: class_name,
        functions: get_functions(node, source_code),
        variable: get_variables(node, source_code),
        attributes: get_attributes(node, source_code),
    };

    if let Some(list) = class.attributes.as_ref()
        && !list.iter().any(|a| a.name == "ToastNode")
    {
        return None; // every class needs to be a ToastNode class
    }

    dbg!(Some(class))
}
fn get_functions(node: tree_sitter::Node, source_code: &str) -> Vec<Function> {
    let mut functions = Vec::new();
    let query = Query::new(
        &tree_sitter_cpp::LANGUAGE.into(),
        "(field_identifier) @field",
    )
    .unwrap();
    let mut cursor = QueryCursor::new();
    let mut captures = cursor.captures(&query, node, source_code.as_bytes());

    while let Some((m, _)) = captures.next() {
        let field_node = m.captures[0].node;

        // check function
        if let Some(parent) = field_node.parent()
            && parent.kind() == "function_declarator"
            && let Some(root) = parent.parent()
        {
            let func = Function {
                name: source_code[field_node.byte_range()].to_string(),
                attributes: get_attributes(root, source_code),
            };
            functions.push(func);
        }
    }
    functions
}

fn get_variables(node: tree_sitter::Node, source_code: &str) -> Vec<Variable> {
    let mut variables = Vec::new();

    let query = Query::new(
        &tree_sitter_cpp::LANGUAGE.into(),
        "(field_identifier) @field",
    )
    .unwrap();
    let mut cursor = QueryCursor::new();
    let mut captures = cursor.captures(&query, node, source_code.as_bytes());

    while let Some((m, _)) = captures.next() {
        let field_node = m.captures[0].node;
        let name = &source_code[field_node.byte_range()];

        // find thing
        if let Some(parent) = field_node.parent()
            && parent.kind() == "field_declaration"
        {
            if let Some(data_type) = parent.child_by_field_name("type")
                && data_type.kind() == "struct_specifier"
            {
                continue;
            }

            let var = Variable {
                name: source_code[field_node.byte_range()].to_string(),
                prefix: get_var_prefix(parent, source_code),
                attributes: get_attributes(parent, source_code),
            };
            variables.push(var);
        }
    }
    variables
}

fn get_var_prefix(node: tree_sitter::Node, source_code: &str) -> String {
    let mut prefix = String::new();
    let mut current_node = Some(node);

    while let Some(parent) = current_node.and_then(|n| n.parent()) {
        if parent.kind() == "struct_specifier"
            && let Some(field_decl) = parent.parent()
            && let Some(declarator) = field_decl.child_by_field_name("declarator")
        {
            let instance_name = &source_code[declarator.byte_range()];
            prefix = format!("{instance_name}.{prefix}");
        }

        if parent.kind() == "class_specifier" {
            prefix = format!("this->{prefix}");
        }

        current_node = Some(parent);
    }
    prefix
}

fn get_attributes(node: tree_sitter::Node, source_code: &str) -> Option<Vec<Attribute>> {
    let mut attributes = Vec::new();

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

            let mut attrib = Attribute {
                name: attrib_name,
                arguments: None,
            };

            if let Some(arg_list) = attr_node
                .child_by_field_name("name")
                .and_then(|n| n.next_named_sibling())
            {
                let mut args_vec = Vec::new();
                for arg in arg_list.children(&mut arg_list.walk()) {
                    if !arg.is_named() {
                        continue;
                    }
                    let arg_text = source_code[arg.byte_range()].to_string();
                    args_vec.push(arg_text);
                }
                if !args_vec.is_empty() {
                    attrib.arguments = Some(args_vec);
                }
            };

            attributes.push(attrib);
        }
    }
    if attributes.is_empty() {
        None
    } else {
        Some(attributes)
    }
}
