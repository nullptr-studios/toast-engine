//! Uses tree-sitter-cpp to extract `[[ToastNode]]` classes, `[[Reflect]]` fields,
//! and tick-function declarations from C++ header files

use tree_sitter::{Parser, Query, QueryCursor, StreamingIterator};

use serde::Serialize;
use serde_json::Value as json_t;

#[derive(Serialize, Clone)]
pub enum FieldType {
    #[serde(rename = "bool_t")]
    Bool,
    #[serde(rename = "int_t")]
    Int,
    #[serde(rename = "float_t")]
    Float,
    #[serde(rename = "string_t")]
    String,
    #[serde(rename = "double_t")]
    Double,
    #[serde(rename = "uid_t")]
    Uid,
    #[serde(rename = "vec2_t")]
    Vec2,
    #[serde(rename = "vec3_t")]
    Vec3,
    #[serde(rename = "vec4_t")]
    Vec4,
    #[serde(rename = "quaternion_t")]
    Quaternion,
}

#[derive(Serialize, Clone)]
pub struct Attribute {
    pub name: String,
    pub args: Vec<String>,
}

#[derive(Serialize, Clone)]
pub struct Parent {
    pub name: String,
    pub namespace: Option<String>,
}

#[derive(Serialize, Clone)]
pub struct Field {
    pub name: String,
    pub typename: String,
    pub field_type: FieldType,
    pub is_array: bool,
    #[serde(skip)]
    pub attributes: Vec<Attribute>,
    #[serde(rename = "attributes")]
    pub attrib_json: json_t,
    pub default: Option<String>,
}

#[derive(Serialize, Clone)]
pub struct Parameter {
    pub name: String,
    #[serde(rename = "type")]
    pub type_name: String,
    pub default: Option<String>,
}

#[derive(Serialize, Clone)]
pub struct Function {
    pub name: String,
    pub return_type: String,
    pub parameters: Vec<Parameter>,
    #[serde(skip)]
    pub attributes: Vec<Attribute>,
    #[serde(rename = "attributes")]
    pub attrib_json: json_t,
    pub is_const: bool,
}

#[derive(Serialize, Clone)]
pub struct Class {
    pub name: String,
    pub namespace: Option<String>,
    pub parent: Option<Parent>,
    pub attributes: Vec<Attribute>,
    pub functions: Vec<String>,
    pub methods: Vec<Function>,
    pub fields: Vec<Field>,
    pub source_file: String,
}
impl Class {
    pub fn qualified_name(&self) -> String {
        if let Some(ns) = &self.namespace {
            format!("{ns}::{}", self.name)
        } else {
            self.name.clone()
        }
    }
}

pub trait AttributeExt {
    fn get_arg(&self, name: &str) -> Option<&String>;
}

impl AttributeExt for [Attribute] {
    fn get_arg(&self, name: &str) -> Option<&String> {
        self.iter()
            .find(|a| a.name == name)
            .and_then(|a| a.args.first())
    }
}

pub fn parse(source: &str, file_path: &str) -> Vec<Class> {
    let mut parser = Parser::new();
    parser
        .set_language(&tree_sitter_cpp::LANGUAGE.into())
        .expect("Language error");

    let tree = parser.parse(source, None).unwrap();
    let query = Query::new(
        &tree_sitter_cpp::LANGUAGE.into(),
        "(class_specifier) @class",
    )
    .unwrap();

    let mut cursor = QueryCursor::new();
    let mut captures = cursor.captures(&query, tree.root_node(), source.as_bytes());

    let mut classes = Vec::new();
    while let Some((m, idx)) = captures.next() {
        let node = m.captures[*idx].node;
        if let Some(class) = get_class(node, source, file_path) {
            classes.push(class);
        }
    }
    classes
}

fn get_class(node: tree_sitter::Node, source: &str, file_path: &str) -> Option<Class> {
    let name = node
        .child_by_field_name("name")
        .map(|n| source[n.byte_range()].to_string())?;

    let all_attrs = get_attributes(node, source);
    if !all_attrs.iter().any(|a| a.name == "ToastNode") {
        return None;
    }
    let attributes = all_attrs
        .into_iter()
        .filter(|a| a.name != "ToastNode")
        .collect();

    Some(Class {
        name,
        namespace: get_namespace(node, source),
        parent: get_parent(node, source),
        attributes,
        functions: get_functions(node, source),
        methods: get_methods(node, source),
        fields: get_fields(node, source),
        source_file: file_path.to_string(),
    })
}

fn get_namespace(node: tree_sitter::Node, source: &str) -> Option<String> {
    let mut current = node.parent();
    while let Some(p) = current {
        if p.kind() == "namespace_definition"
            && let Some(name_node) = p.child_by_field_name("name")
        {
            return Some(source[name_node.byte_range()].to_string());
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
                        Some(Parent {
                            name: full.to_string(),
                            namespace: None,
                        })
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
    // must match TickFunctionList in reflect.hpp; add new lifecycle methods here too
    const TICK_FUNCTIONS: &[&str] = &[
        "preInit",
        "init",
        "begin",
        "earlyTick",
        "tick",
        "postPhysics",
        "lateTick",
        "end",
        "destroy",
        "onEnable",
        "onDisable",
        "load",
        "save",
    ];

    let query = Query::new(
        &tree_sitter_cpp::LANGUAGE.into(),
        "(field_identifier) @field",
    )
    .unwrap();
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

fn get_methods(node: tree_sitter::Node, source: &str) -> Vec<Function> {
    let query = Query::new(
        &tree_sitter_cpp::LANGUAGE.into(),
        "(function_declarator) @fn",
    )
    .unwrap();
    let mut cursor = QueryCursor::new();
    let mut captures = cursor.captures(&query, node, source.as_bytes());

    let mut methods = Vec::new();
    while let Some((m, _)) = captures.next() {
        let func_decl = m.captures[0].node;

        let Some(name_node) = func_decl.child_by_field_name("declarator") else {
            continue;
        };
        if name_node.kind() != "field_identifier" {
            continue;
        }

        let Some(decl) = enclosing_declaration(func_decl) else {
            continue;
        };
        let all_attrs = get_attributes(decl, source);
        if !all_attrs.iter().any(|a| a.name == "Reflect") {
            continue;
        }
        let attributes: Vec<Attribute> = all_attrs
            .into_iter()
            .filter(|a| a.name != "Reflect")
            .collect();

        let name = source[name_node.byte_range()].to_string();
        if methods.iter().any(|f: &Function| f.name == name) {
            continue;
        }

        methods.push(Function {
            name,
            return_type: get_return_type(decl, func_decl, source),
            parameters: get_parameters(func_decl, source),
            is_const: is_const_method(func_decl, source),
            attributes: attributes.clone(),
            attrib_json: attrs_to_json(&attributes),
        });
    }
    methods
}

fn enclosing_declaration(func_decl: tree_sitter::Node) -> Option<tree_sitter::Node> {
    let mut current = func_decl.parent();
    while let Some(p) = current {
        match p.kind() {
            "field_declaration" | "function_definition" | "declaration" => return Some(p),
            _ => current = p.parent(),
        }
    }
    None
}

fn get_return_type(decl: tree_sitter::Node, func_decl: tree_sitter::Node, source: &str) -> String {
    let base = decl
        .child_by_field_name("type")
        .map(|t| source[t.byte_range()].trim().to_string())
        .unwrap_or_default();

    let mut suffix = String::new();
    let mut current = decl.child_by_field_name("declarator");
    while let Some(node) = current {
        if node.id() == func_decl.id() {
            break;
        }
        match node.kind() {
            "pointer_declarator" => suffix.push('*'),
            "reference_declarator" => suffix.push('&'),
            _ => {}
        }
        current = node.child_by_field_name("declarator");
    }

    format!("{base}{suffix}")
}

fn is_const_method(func_decl: tree_sitter::Node, source: &str) -> bool {
    for child in func_decl.children(&mut func_decl.walk()) {
        if child.kind() == "type_qualifier" && source[child.byte_range()].trim() == "const" {
            return true;
        }
    }
    false
}

fn get_parameters(func_decl: tree_sitter::Node, source: &str) -> Vec<Parameter> {
    let Some(params) = func_decl.child_by_field_name("parameters") else {
        return Vec::new();
    };

    let mut result = Vec::new();
    for param in params.children(&mut params.walk()) {
        if param.kind() != "parameter_declaration"
            && param.kind() != "optional_parameter_declaration"
        {
            continue;
        }

        let default = param
            .child_by_field_name("default_value")
            .map(|d| source[d.byte_range()].trim().to_string());

        let name_node = param
            .child_by_field_name("declarator")
            .and_then(find_identifier);

        let (name, type_name) = if let Some(n) = name_node {
            let ty = source[param.start_byte()..n.start_byte()]
                .trim()
                .to_string();
            (source[n.byte_range()].to_string(), ty)
        } else {
            // Unnamed parameter: take the type text up to the default value, if any
            let end = param
                .child_by_field_name("default_value")
                .map(|d| d.start_byte())
                .unwrap_or_else(|| param.end_byte());
            let ty = source[param.start_byte()..end]
                .trim()
                .trim_end_matches('=')
                .trim()
                .to_string();
            (String::new(), ty)
        };

        result.push(Parameter {
            name,
            type_name,
            default,
        });
    }
    result
}

// Finds the first identifier node in a declarator subtree
fn find_identifier(node: tree_sitter::Node) -> Option<tree_sitter::Node> {
    if node.kind() == "identifier" {
        return Some(node);
    }
    for child in node.children(&mut node.walk()) {
        if let Some(found) = find_identifier(child) {
            return Some(found);
        }
    }
    None
}

fn get_fields(node: tree_sitter::Node, source: &str) -> Vec<Field> {
    let query = Query::new(
        &tree_sitter_cpp::LANGUAGE.into(),
        "(field_identifier) @field",
    )
    .unwrap();
    let mut cursor = QueryCursor::new();
    let mut captures = cursor.captures(&query, node, source.as_bytes());

    let mut fields = Vec::new();
    while let Some((m, _)) = captures.next() {
        let field_node = m.captures[0].node;
        let Some(parent) = field_node.parent() else {
            continue;
        };
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
        if !all_attrs.iter().any(|a| a.name == "Reflect") {
            continue;
        }
        let attributes: Vec<Attribute> = all_attrs
            .into_iter()
            .filter(|a| a.name != "Reflect")
            .collect();

        let type_name = parent
            .child_by_field_name("type")
            .map(|t| source[t.byte_range()].trim().to_string())
            .unwrap_or_default();

        let default = parent
            .child_by_field_name("default_value")
            .map(|d| source[d.byte_range()].trim().to_string());

        fields.push(Field {
            name: source[field_node.byte_range()].to_string(),
            typename: type_name.clone(),
            field_type: infer_field_type(&type_name),
            is_array: type_name.contains("vector<"),
            attributes: attributes.clone(),
            attrib_json: attrs_to_json(&attributes),
            default,
        });
    }
    fields
}

fn get_attributes(node: tree_sitter::Node, source: &str) -> Vec<Attribute> {
    // two-level walk: attribute_declaration wraps one or more attribute nodes
    let mut attributes = Vec::new();

    for list in node.children(&mut node.walk()) {
        if list.kind() != "attribute_declaration" {
            continue;
        }
        for attr_node in list.children(&mut list.walk()) {
            if attr_node.kind() != "attribute" {
                continue;
            }
            let Some(name_node) = attr_node.child_by_field_name("name") else {
                continue;
            };
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

// tree-sitter includes surrounding double-quotes in string literal nodes; strip them
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

pub fn attrs_to_json(attrs: &[Attribute]) -> json_t {
    let map: serde_json::Map<std::string::String, json_t> = attrs
        .iter()
        .map(|a| (a.name.clone(), serde_json::json!(a.args)))
        .collect();
    json_t::Object(map)
}

fn infer_field_type(type_name: &str) -> FieldType {
    let base = type_name
        .trim()
        .trim_start_matches("toast::")
        .trim_start_matches("std::")
        .trim_start_matches("glm::");

    // both are held by value in C++ but the reflection boundary only exchanges UIDs;
    // the accessor resolves/unresolves the handle on get/set
    if type_name.contains("AssetHandle<") {
        return FieldType::Uid;
    }
    if base.starts_with("Box<") {
        return FieldType::Uid;
    }

    match base {
        "bool" => FieldType::Bool,
        "int" | "int8_t" | "int16_t" | "int32_t" | "int64_t" | "uint8_t" | "uint16_t"
        | "uint32_t" | "uint64_t" => FieldType::Int,
        "float" => FieldType::Float,
        "string" => FieldType::String,
        "double" => FieldType::Double,
        "UID" => FieldType::Uid,
        "vec2" => FieldType::Vec2,
        "vec3" => FieldType::Vec3,
        "vec4" => FieldType::Vec4,
        "quat" | "quaternion" => FieldType::Quaternion,
        _ => FieldType::Int, // unknown types silently become Int; accessor compiles but the inspector widget will be wrong
    }
}
