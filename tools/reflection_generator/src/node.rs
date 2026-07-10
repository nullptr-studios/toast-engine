use crate::parser::*;
use serde::Serialize;
use serde_json::Value as json_t;
use serde_json::*;

use std::collections::BTreeMap;

#[derive(Serialize)]
pub struct NodeInfo {
    pub name: String,
    pub namespace: Option<String>,
    pub parent: Option<Parent>,
    pub attributes: json_t,
    pub functions: TickFunctions,
    pub methods: Vec<Function>,
    /// Path relative to --include-root
    pub source_file: String,
    pub groups: Vec<GroupInfo>,
    pub global_fields: Vec<Field>,
    pub is_interface: bool,
}

#[derive(Serialize)]
pub struct TickFunctions {
    pub pre_init: bool,
    pub init: bool,
    pub begin: bool,
    pub early_tick: bool,
    pub tick: bool,
    pub post_physics: bool,
    pub late_tick: bool,
    pub end: bool,
    pub destroy: bool,
    pub on_enable: bool,
    pub on_disable: bool,
    pub load: bool,
    pub save: bool,
}

#[derive(Serialize)]
pub struct GroupInfo {
    pub name: String,
    pub subgroups: Vec<SubgroupInfo>,
    pub fields: Vec<Field>,
}

#[derive(Serialize)]
pub struct SubgroupInfo {
    pub name: String,
    pub fields: Vec<Field>,
}

const RESERVED_FIELD_NAMES: &[&str] = &[
    "m_uid",
    "m_name",
    "m_local_enabled",
    "m_parent",
    "m_source_prefab",
];

// TODO: smth with these
fn to_snake(s: &str) -> std::string::String {
    s.to_lowercase().replace(' ', "_")
}
fn cpp_escape(s: &str) -> std::string::String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}

pub fn build_template_context(node: &NodeInfo) -> json_t {
    // --- 1. Basic Info & Namespaces ---
    let snake_name = node.name.to_lowercase();
    let qualified_name = node.namespace.as_ref().map_or(node.name.clone(), |ns| format!("{ns}::{}", node.name));
    let parent_qualified_name = node.parent.as_ref().map(|p| p.namespace.as_ref().map_or(p.name.clone(), |ns| format!("{ns}::{}", p.name)));
    let parent_snake_name = node.parent.as_ref().map(|p| p.name.to_lowercase());

    // --- 2. Field Flattening & Asset Tracking ---
    let mut all_fields_flat = Vec::new();
    let mut has_asset_handle = false;
    // This inline closure updates the flat list, checks for assets, and returns the index
    let mut process_field = |f: &Field| -> usize {
        let idx = all_fields_flat.len();
        if f.typename.contains("AssetHandle<") { has_asset_handle = true; }

        let attrs_list: Vec<json_t> = match &f.attrib_json {
            json_t::Object(map) => map.iter().map(|(k, v)| json!({ "name": k, "args": v })).collect(),
            _ => vec![],
        };

        all_fields_flat.push(json!({
            "index":           idx,
            "name":            f.name,
            "member_name":     f.name,
            "typename":        f.typename,
            "field_type":      serde_json::to_value(&f.field_type).unwrap(),
            "is_array":        f.is_array,
            "is_asset_handle": f.typename.contains("AssetHandle<"),
            "attributes":      f.attrib_json,
            "attrs_list":      attrs_list,
            "default":         f.default,
        }));
        idx
    };

    // --- 3. Process Fields (Global, Groups, Subgroups) ---
    let global_field_indices: Vec<usize> = node.global_fields.iter().map(&mut process_field).collect();

    let augmented_groups: Vec<json_t> = node.groups.iter().map(|g| {
        json!({
            "name":          g.name,
            "snake_name":    to_snake(&g.name),
            "field_indices": g.fields.iter().map(&mut process_field).collect::<Vec<usize>>(),
            "subgroups":     g.subgroups.iter().map(|sg| {
                json!({
                    "name":          sg.name,
                    "snake_name":    to_snake(&sg.name),
                    "field_indices": sg.fields.iter().map(&mut process_field).collect::<Vec<usize>>(),
                })
            }).collect::<Vec<json_t>>(),
        })
    }).collect();
    // --- 4. Active Tick Functions ---
    let tf = &node.functions;
    let active_tick_fns: Vec<json_t> = [
        (tf.load,         "load",         "load"),
        (tf.save,         "save",         "save"),
        (tf.pre_init,     "pre_init",     "preInit"),
        (tf.init,         "init",         "init"),
        (tf.destroy,      "destroy",      "destroy"),
        (tf.begin,        "begin",        "begin"),
        (tf.end,          "end",          "end"),
        (tf.on_enable,    "on_enable",    "onEnable"),
        (tf.on_disable,   "on_disable",   "onDisable"),
        (tf.early_tick,   "early_tick",   "earlyTick"),
        (tf.tick,         "tick",         "tick"),
        (tf.post_physics, "post_physics", "postPhysics"),
        (tf.late_tick,    "late_tick",    "lateTick"),
    ]
        .into_iter()
        .filter(|(is_active, _, _)| *is_active)
        .map(|(_, flag, method)| json!({ "flag": flag, "method": method }))
        .collect();

    // --- 5. Reflected Methods ---
    let methods_ctx: Vec<json_t> = node.methods.iter().map(|m| {
        json!({
            "name":        m.name,
            "return_type": m.return_type,
            "is_const":    m.is_const,
            "parameters":  m.parameters.iter().enumerate().map(|(i, p)| {
                json!({
                    "index":           i,
                    "arg_name":        format!("a{i}"),
                    "name":            p.name,
                    "type":            p.type_name,
                    "default":         p.default,
                    "default_escaped": p.default.as_deref().map(cpp_escape),
                })
            }).collect::<Vec<json_t>>(),
        })
    }).collect();

    // --- 6. Final Payload Assembly ---
    json!({
        "name":                  node.name,
        "namespace":             node.namespace,
        "snake_name":            snake_name,
        "qualified_name":        qualified_name,
        "parent_qualified_name": parent_qualified_name,
        "parent_snake_name":     parent_snake_name,
        "source_file":           node.source_file,
        "attributes":            node.attributes,
        "all_fields_flat":       all_fields_flat,
        "global_field_indices":  global_field_indices,
        "groups":                augmented_groups,
        "active_tick_fns":       active_tick_fns,
        "methods":               methods_ctx,
        "has_asset_handle":      has_asset_handle,
        "is_interface":          node.is_interface,
    })
}

pub fn build_node(class: &Class) -> NodeInfo {
    let is_interface = class.attributes.iter().any(|a| a.name == "Interface");
    let (global_fields, groups) = build_field_groups(&class.fields);

    NodeInfo {
        name: class.name.clone(),
        namespace: class.namespace.clone(),
        parent: class.parent.clone(),
        attributes: attrs_to_json(&class.attributes),
        functions: build_tick_functions(class),
        methods: class.methods.clone(),
        groups,
        global_fields,
        source_file: class.source_file.clone(),
        is_interface,
    }
}

pub fn validate_class(class: &Class) -> std::result::Result<(), String> {
    // Fully qualified name
    let q = class.qualified_name();

    // Exclude Base Class
    if q == "toast::Node" {
        return Ok(());
    }

    for f in &class.fields {
        // Reject reserved field names
        if RESERVED_FIELD_NAMES.contains(&f.name.as_str()) {
            #[rustfmt::skip]
            let msg = format!("{q}: reflected field '{}' uses a reserved engine member name (only toast::Node may)", f.name);
            return Err(msg);
        }
        // Reject internal group syntax (~)
        if let Some(g) = f.attributes.get_arg("Group").filter(|g| g.starts_with('~')) {
            #[rustfmt::skip]
            let msg = format!("{q}: group name '{g}' may not start with '~' (reserved)");
            return Err(msg);
        }
    }

    Ok(())
}

fn build_tick_functions(class: &Class) -> TickFunctions {
    let fns = &class.functions;
    TickFunctions {
        pre_init: fns.contains(&"preInit".to_string()),
        init: fns.contains(&"init".to_string()),
        begin: fns.contains(&"begin".to_string()),
        early_tick: fns.contains(&"earlyTick".to_string()),
        tick: fns.contains(&"tick".to_string()),
        post_physics: fns.contains(&"postPhysics".to_string()),
        late_tick: fns.contains(&"lateTick".to_string()),
        end: fns.contains(&"end".to_string()),
        destroy: fns.contains(&"destroy".to_string()),
        on_enable: fns.contains(&"onEnable".to_string()),
        on_disable: fns.contains(&"onDisable".to_string()),
        load: fns.contains(&"load".to_string()),
        save: fns.contains(&"save".to_string()),
    }
}

fn build_field_groups(fields: &[Field]) -> (Vec<Field>, Vec<GroupInfo>) {
    #[derive(Default)]
    struct GroupBucket {
        fields: Vec<Field>,
        subgroups: BTreeMap<String, Vec<Field>>,
    }
    let mut global_fields = Vec::new();
    let mut group_map: BTreeMap<String, GroupBucket> = BTreeMap::new();
    for field in fields {
        let group = field.attributes.get_arg("Group").cloned();
        let subgroup = field.attributes.get_arg("Subgroup").cloned();
        let fi = field.clone();

        if let Some(g) = group {
            let bucket = group_map.entry(g).or_default();
            match subgroup {
                Some(sg) => bucket.subgroups.entry(sg).or_default().push(fi),
                None => bucket.fields.push(fi),
            }
        } else {
            global_fields.push(fi);
        }
    }

    let groups = group_map
        .into_iter()
        .map(|(name, bucket)| {
            let subgroups = bucket
                .subgroups
                .into_iter()
                .map(|(sg_name, sg_fields)| SubgroupInfo {
                    name: sg_name,
                    fields: sg_fields,
                })
            .collect();

            GroupInfo {
                name,
                fields: bucket.fields,
                subgroups,
        }
        })
    .collect();

    (global_fields, groups)
}
