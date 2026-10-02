//! Converts parsed Class structs into NodeInfo data and emits C++ files via Jinja2 templates

use crate::*;
use minijinja::Environment;
use std::collections::HashMap;
use std::fs;
use std::path::Path;

pub fn generate_files(nodes: &[NodeInfo], output: &Path, register_fn: &str, split_typeinfo: bool) {
    generate_files_with_events(nodes, &[], output, register_fn, split_typeinfo);
}

pub fn generate_files_with_events(nodes: &[NodeInfo], events: &[EventInfo], output: &Path, register_fn: &str, split_typeinfo: bool) {
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
    let node_tmpl = env
        .get_template("node.generated.hpp.jinja2")
        .unwrap_or_else(|e| panic!("cannot load node.generated.hpp.jinja2: {e}"));

    let type_info_tmpl = if split_typeinfo {
        Some(
            env.get_template("node.type_info.cpp.jinja2")
                .unwrap_or_else(|e| panic!("cannot load node.type_info.cpp.jinja2: {e}")),
        )
    } else {
        None
    };

    for node in nodes {
        let mut ctx = build_template_context(node);
        if split_typeinfo && let json_t::Object(map) = &mut ctx {
            map.insert("split_typeinfo".to_string(), json_t::Bool(true));
        }
        let sn = ctx["snake_name"].as_str().unwrap();

        let out = output.join(format!("{sn}.generated.hpp"));
        let text = node_tmpl
            .render(&ctx)
            .unwrap_or_else(|e| panic!("template error for {}: {e}", node.class.name));
        fs::write(&out, text).unwrap_or_else(|e| panic!("cannot write {}: {e}", out.display()));

        if let Some(tmpl) = &type_info_tmpl {
            let cpp_out = output.join(format!("{sn}.type_info.cpp"));
            let cpp_text = tmpl.render(&ctx).unwrap_or_else(|e| {
                panic!(
                    "template error for type_info.cpp ({}): {e}",
                    node.class.name
                )
            });
            fs::write(&cpp_out, cpp_text)
                .unwrap_or_else(|e| panic!("cannot write {}: {e}", cpp_out.display()));
        }
    }

    // reflect.generated.cpp
    let cpp_tmpl = env
        .get_template("reflect.generated.cpp.jinja2")
        .unwrap_or_else(|e| panic!("cannot load reflect.generated.cpp.jinja2: {e}"));

    let mut all_ctx: Vec<json_t> = nodes.iter().map(build_template_context).collect();
    if split_typeinfo {
        for ctx in &mut all_ctx {
            if let json_t::Object(map) = ctx {
                map.insert("split_typeinfo".to_string(), json_t::Bool(true));
            }
        }
    }
    let event_ctx: Vec<json_t> = events.iter().filter(|e| e.supported).map(|e| json!({
        "name": e.name,
        "qualified_name": e.qualified_name(),
        "source_file": e.source_file,
        "sendable": e.sendable,
        "fields": e.fields,
    })).collect();
    let cpp_ctx = serde_json::json!({
        "nodes":          all_ctx,
        "events":         event_ctx,
        "register_fn":    register_fn,
        "split_typeinfo": split_typeinfo,
    });

    let out = output.join("reflect.generated.cpp");
    let text = cpp_tmpl
        .render(&cpp_ctx)
        .unwrap_or_else(|e| panic!("template error for reflect.generated.cpp: {e}"));
    fs::write(&out, text).unwrap_or_else(|e| panic!("cannot write {}: {e}", out.display()));
}

fn qualify(namespace: Option<&str>, name: &str) -> String {
    match namespace {
        Some(ns) => format!("{ns}::{name}"),
        None => name.to_string(),
    }
}

fn resolve_parent(node: &NodeInfo, known: &HashMap<String, usize>) -> Option<usize> {
    let parent = node.class.parent.as_ref()?;
    let written = qualify(parent.namespace.as_deref(), &parent.name);
    if let Some(global) = written.strip_prefix("::") {
        return known.get(global).copied();
    }
    let mut scope = node.class.namespace.as_deref();
    loop {
        let candidate = match scope {
            Some(ns) => format!("{ns}::{written}"),
            None => written.clone(),
        };
        if let Some(&idx) = known.get(&candidate) {
            return Some(idx);
        }
        scope = match scope {
            Some(ns) => ns.rfind("::").map(|pos| &ns[..pos]),
            None => return None,
        };
    }
}

pub fn topological_sort(nodes: Vec<NodeInfo>) -> Vec<NodeInfo> {
    let known: HashMap<String, usize> = nodes
        .iter()
        .enumerate()
        .map(|(i, n)| (qualify(n.class.namespace.as_deref(), &n.class.name), i))
        .collect();
    let parents: Vec<Option<usize>> = nodes.iter().map(|n| resolve_parent(n, &known)).collect();

    let mut placed = vec![false; nodes.len()];
    let mut order = Vec::with_capacity(nodes.len());
    for start in 0..nodes.len() {
        let mut chain = Vec::new();
        let mut current = Some(start);
        while let Some(i) = current {
            if placed[i] || chain.contains(&i) {
                break;
            }
            chain.push(i);
            current = parents[i];
        }
        for &i in chain.iter().rev() {
            placed[i] = true;
            order.push(i);
        }
    }

    let mut slots: Vec<Option<NodeInfo>> = nodes.into_iter().map(Some).collect();
    order.into_iter().map(|i| slots[i].take().expect("node placed twice")).collect()
}
