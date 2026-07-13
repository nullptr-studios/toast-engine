//! Re-exports for the integration test crate

mod generator;
mod node;
mod parser;

pub use generator::*;
pub use node::*;
pub use parser::*;
use serde::Serialize;
use serde_json::to_value;
pub use serde_json::{Value as json_t, json};

#[derive(Serialize, Clone)]
pub enum FieldType {
    #[serde(rename = "bool_t")] Bool,
    #[serde(rename = "int_t")] Int,
    #[serde(rename = "float_t")] Float,
    #[serde(rename = "string_t")] String,
    #[serde(rename = "double_t")] Double,
    #[serde(rename = "uid_t")] Uid,
    #[serde(rename = "vec2_t")] Vec2,
    #[serde(rename = "vec3_t")] Vec3,
    #[serde(rename = "vec4_t")] Vec4,
    #[serde(rename = "quaternion_t")] Quaternion,
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
    #[serde(skip)]
    pub attributes: Vec<Attribute>,
    #[serde(rename = "attributes")]
    pub attrib_json: json_t,
    pub functions: Vec<String>,
    pub methods: Vec<Function>,
    pub fields: Vec<Field>,
    pub source_file: String,
}

// tree-sitter sees TOAST_API and __declspec attributes as identifiers that break field parsing
pub fn strip_export_macros(source: &str) -> String {
    source
        .replace("TOAST_API ", "")
        .replace("__declspec(dllexport) ", "")
        .replace("__declspec(dllimport) ", "")
}


pub fn generate_json(nodes: &[NodeInfo]) -> json_t {
    to_value(nodes).unwrap_or(json_t::Array(vec![]))
}
