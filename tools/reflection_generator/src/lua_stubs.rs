use crate::node::NodeInfo;
use crate::{EventInfo, Field, FieldType, Function, Signal, json};
use minijinja::Environment;

/// Strips namespaces
fn bare(name: &str) -> &str {
    name.rsplit("::").next().unwrap_or(name)
}

/// Strips the m_ member prefix
fn lua_field_name(name: &str) -> &str {
    name.strip_prefix("m_").unwrap_or(name)
}

fn has_color_attr(field: &Field) -> bool {
    field.attributes.iter().any(|a| a.name == "Color")
}

/// Lua type of a reflected field
fn field_lua_type(field: &Field) -> String {
    let base = match field.field_type {
        FieldType::Bool => "boolean".to_string(),
        FieldType::Int => "integer".to_string(),
        FieldType::Float | FieldType::Double => "number".to_string(),
        FieldType::String => "string".to_string(),
        FieldType::Vec2 => "vec2".to_string(),
        FieldType::Vec3 => if has_color_attr(field) {
            "color3"
        } else {
            "vec3"
        }
        .to_string(),
        FieldType::Vec4 => if has_color_attr(field) {
            "color4"
        } else {
            "vec4"
        }
        .to_string(),
        FieldType::Quaternion => "quat".to_string(),
        FieldType::Uid => uid_lua_type(&field.typename),
    };
    if field.is_array {
        format!("{base}[]")
    } else {
        base
    }
}

/// Lua type of a Box<T> and Handle<T> field
fn uid_lua_type(typename: &str) -> String {
    let inner = typename
        .trim_start_matches("std::vector<")
        .trim_end_matches('>');
    if let Some(node_type) = inner.strip_prefix("Box<") {
        return bare(node_type.trim_end_matches('>')).to_string();
    }
    if inner.contains("Handle<") {
        return "Asset".to_string();
    }
    "any".to_string()
}

/// Lua type of a method parameter or return
fn cpp_lua_type(cpp: &str) -> String {
    let t = cpp.trim();
    if t.contains("input::ActionEvent") {
        return "InputActionEvent".to_string();
    }
    if t.contains("input::Action") {
        return "InputAction".to_string();
    }
    if t.contains("input::Bind") {
        return "InputBind".to_string();
    }
    if t.contains("input::KeyCode") {
        return "InputKeyCode".to_string();
    }
    if t.contains("input::Device") {
        return "InputDeviceValue".to_string();
    }
    if t.contains("input::ValueType") {
        return "InputValueTypeValue".to_string();
    }
    if t.contains("input::ModifierKey") {
        return "InputModifierValue".to_string();
    }
    if t.contains("input::InputKind") {
        return "InputKindValue".to_string();
    }
    if t.contains("Box<") {
        let inner = t.split("Box<").nth(1).unwrap_or("Node");
        return bare(inner.trim_end_matches(['>', '&', ' '])).to_string();
    }
    if t.contains("Handle<") {
        return "Asset".to_string();
    }
    if t.contains("bool") {
        return "boolean".to_string();
    }
    if t.contains("float") || t.contains("double") {
        return "number".to_string();
    }
    if t.contains("string") {
        return "string".to_string();
    }
    if t.contains("vec4") {
        return "vec4".to_string();
    }
    if t.contains("vec3") {
        return "vec3".to_string();
    }
    if t.contains("vec2") {
        return "vec2".to_string();
    }
    if t.contains("quat") {
        return "quat".to_string();
    }
    if t == "void" {
        return "nil".to_string();
    }
    if t.contains("int") || t.contains("long") || t.contains("short") || t == "char" {
        return "integer".to_string();
    }
    "any".to_string()
}

fn method_signature(class_name: &str, method: &Function) -> String {
    let mut params = format!("self: {class_name}");
    for p in &method.parameters {
        let optional = if p.default.is_some() { "?" } else { "" };
        params.push_str(&format!(
            ", {}{optional}: {}",
            p.name,
            cpp_lua_type(&p.type_name)
        ));
    }
    let ret = cpp_lua_type(&method.return_type);
    if ret == "nil" {
        format!("fun({params})")
    } else {
        format!("fun({params}): {ret}")
    }
}

fn signal_lua_type(signal: &Signal) -> String {
    let args: Vec<String> = signal
        .arguments
        .iter()
        .map(|arg| cpp_lua_type(arg))
        .collect();
    match args.len() {
        0 => "Signal0".to_string(),
        1..=4 => format!("Signal{}<{}>", args.len(), args.join(", ")),
        _ => "Signal0".to_string(),
    }
}

fn field_context(field: &Field) -> Option<serde_json::Value> {
    // Hidden fields don't appear in the inspector and shadow builtins like uid()/name()
    if field.attributes.iter().any(|a| a.name == "Hidden") {
        return None;
    }
    Some(json!({
        "name": lua_field_name(&field.name),
        "lua_type": field_lua_type(field),
    }))
}

fn render_template(name: &str, source: &'static str, context: &serde_json::Value) -> String {
    let mut env = Environment::new();
    env.set_trim_blocks(true);
    env.set_lstrip_blocks(true);
    env.add_template(name, source)
        .unwrap_or_else(|e| panic!("cannot load {name}: {e}"));
    env.get_template(name)
        .unwrap_or_else(|e| panic!("cannot get {name}: {e}"))
        .render(context)
        .unwrap_or_else(|e| panic!("template error for {name}: {e}"))
}

/// Renders the full types.d.lua contents for the reflected node set
pub fn generate_lua_stubs(nodes: &[NodeInfo]) -> String {
    let node_contexts: Vec<_> = nodes
        .iter()
        .map(|node| {
            let fields: Vec<_> = node
                .global_fields
                .iter()
                .chain(node.groups.iter().flat_map(|group| {
                    group
                        .fields
                        .iter()
                        .chain(group.subgroups.iter().flat_map(|subgroup| &subgroup.fields))
                }))
                .filter_map(field_context)
                .collect();
            let methods: Vec<_> = node
                .class
                .methods
                .iter()
                .map(|method| {
                    json!({
                        "name": method.name,
                        "signature": method_signature(&node.class.name, method),
                    })
                })
                .collect();
            let signals: Vec<_> = node
                .class
                .signals
                .iter()
                .map(|signal| {
                    json!({
                        "name": signal.name,
                        "lua_type": signal_lua_type(signal),
                    })
                })
                .collect();
            json!({
                "name": node.class.name,
                "inheritance": node.class.parent.as_ref()
                    .map(|parent| format!(" : {}", bare(&parent.name)))
                    .unwrap_or_default(),
                "fields": fields,
                "methods": methods,
                "signals": signals,
            })
        })
        .collect();

    render_template(
        "types.d.lua.jinja2",
        include_str!("../templates/types.d.lua.jinja2"),
        &json!({ "nodes": node_contexts }),
    )
}

pub fn generate_event_lua_stubs(events: &[EventInfo]) -> String {
    let event_contexts: Vec<_> = events
        .iter()
        .filter(|event| event.supported)
        .map(|event| {
            let fields: Vec<_> = event
                .fields
                .iter()
                .map(|field| {
                    json!({
                        "name": field.name,
                        "lua_type": field.lua_type.as_deref().unwrap_or("any"),
                    })
                })
                .collect();
            json!({
                "name": event.name,
                "fields": fields,
            })
        })
        .collect();

    render_template(
        "events.d.lua.jinja2",
        include_str!("../templates/events.d.lua.jinja2"),
        &json!({ "events": event_contexts }),
    )
}
