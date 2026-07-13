//! Converts parsed Class structs into NodeInfo data and emits C++ files via Jinja2 templates

use crate::node::*;
use crate::parser::*;
use minijinja::Environment;
use serde_json::Value as json_t;
use serde_json::to_value;
use std::fs;
use std::path::Path;

pub fn generate_json(nodes: &[NodeInfo]) -> json_t {
    to_value(nodes).unwrap_or(json_t::Array(vec![]))
}

pub fn generate_files(nodes: &[NodeInfo], output: &Path, register_fn: &str, split_typeinfo: bool) {
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
        if split_typeinfo {
            if let json_t::Object(map) = &mut ctx {
                map.insert("split_typeinfo".to_string(), json_t::Bool(true));
            }
        }
        let sn = ctx["snake_name"].as_str().unwrap();

        let out = output.join(format!("{sn}.generated.hpp"));
        let text = node_tmpl
            .render(&ctx)
            .unwrap_or_else(|e| panic!("template error for {}: {e}", node.name));
        fs::write(&out, text).unwrap_or_else(|e| panic!("cannot write {}: {e}", out.display()));

        if let Some(tmpl) = &type_info_tmpl {
            let cpp_out = output.join(format!("{sn}.type_info.cpp"));
            let cpp_text = tmpl.render(&ctx).unwrap_or_else(|e| {
                panic!("template error for type_info.cpp ({}): {e}", node.name)
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
    let cpp_ctx = serde_json::json!({
        "nodes":       all_ctx,
        "register_fn": register_fn,
    });

    let out = output.join("reflect.generated.cpp");
    let text = cpp_tmpl
        .render(&cpp_ctx)
        .unwrap_or_else(|e| panic!("template error for reflect.generated.cpp: {e}"));
    fs::write(&out, text).unwrap_or_else(|e| panic!("cannot write {}: {e}", out.display()));
}
