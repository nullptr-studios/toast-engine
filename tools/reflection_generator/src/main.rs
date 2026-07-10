//! CLI driver; walks input dirs for `[[ToastNode]]` headers, parses with tree-sitter,
//! validates against reserved names, emits one `.generated.hpp` per class and one
//! `reflect.generated.cpp` registration file

use reflection_generator::*;

use clap::Parser;
use walkdir::WalkDir;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Parser)]
#[command(name = "refgen", about = "Toast reflection code generator")]
struct Cli {
    /// Path to the JSON metadata output file
    #[arg(long)]
    database: PathBuf,

    /// Directory where generated files are written
    #[arg(long)]
    output: PathBuf,

    /// Directory to search for headers
    #[arg(long = "input")]
    inputs: Vec<PathBuf>,

    /// Root directory used to compute #include paths
    #[arg(long)]
    include_root: Option<PathBuf>,

    /// Name of the registration function to emit
    #[arg(long, default_value = "registerEngineTypes")]
    register_fn: std::string::String,

    #[arg(long)]
    split_typeinfo: bool,
    #[arg(long = "attribute")]
    attributes: Vec<std::string::String>,
}

fn main() {
    let cli = Cli::parse();

    // Validate paths
    for input in &cli.inputs {
        if !input.is_dir() {
            eprintln!("error: --input '{}' is not a directory", input.display());
            std::process::exit(1);
        }
    }
    if !cli.output.is_dir() {
        eprintln!("error: --output '{}' is not a directory", cli.output.display());
        std::process::exit(1);
    }

    // Discover headers
    let header_files = find_headers(&cli.inputs);
    if header_files.is_empty() {
        eprintln!("warning: no [[ToastNode]] headers found in the given --input directories");
    }

    // Parse each file
    let mut all_nodes: Vec<NodeInfo> = Vec::new();

    for file_path in &header_files {
        let include_path = compute_include_path(file_path, cli.include_root.as_deref());

        let source = match fs::read_to_string(file_path) {
            Ok(s)  => s,
            Err(e) => { eprintln!("error reading '{}': {e}", file_path.display()); continue; }
        };

        let preprocessed = strip_export_macros(&source);
        let classes = parse(&preprocessed,&include_path);

        for class in &classes {
            if let Err(msg) = validate_class(class) {
                eprintln!("error: {msg}");
                std::process::exit(1);
            }
            let mut node = build_node(class);
            inject_attributes(&mut node, &cli.attributes);
            all_nodes.push(node);
        }
    }

    // Write JSON database
    if let Some(parent) = cli.database.parent()
        && !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent)
                .unwrap_or_else(|e| eprintln!("warning: cannot create database directory: {e}"));
        }
    let json = serde_json::to_string_pretty(&generate_json(&all_nodes))
        .expect("JSON serialisation failed");
    fs::write(&cli.database, json)
        .unwrap_or_else(|e| eprintln!("warning: cannot write database '{}': {e}", cli.database.display()));

    // Generate files, sorted so base classes are included before derived classes
    let all_nodes = topological_sort(all_nodes);
    generate_files(&all_nodes, &cli.output, &cli.register_fn, cli.split_typeinfo);

    println!(
        "refgen: generated {} type(s) → '{}' (fn: {})",
        all_nodes.len(),
        cli.output.display(),
        cli.register_fn
    );
}

/// Inject extra attributes
fn inject_attributes(node: &mut NodeInfo, attributes: &[std::string::String]) {
    if attributes.is_empty() {
        return;
    }
    if !node.attributes.is_object() {
        node.attributes = serde_json::Value::Object(serde_json::Map::new());
    }
    let map = node.attributes.as_object_mut().expect("attributes is an object");
    for name in attributes {
        map.entry(name.clone()).or_insert_with(|| serde_json::json!([]));
    }
}

/// Walk all input dirs and return paths of .hpp files containing [[ToastNode]]
fn find_headers(inputs: &[PathBuf]) -> Vec<PathBuf> {
    let mut result = Vec::new();
    for dir in inputs {
        for entry in WalkDir::new(dir).into_iter().filter_map(|e| e.ok()) {
            let path = entry.path().to_path_buf();
            if path.extension().and_then(|e| e.to_str()) != Some("hpp") {
                continue;
            }
            if let Ok(content) = fs::read_to_string(&path)
                && content.contains("ToastNode") {
                    result.push(path);
                }
        }
    }
    result
}

fn topological_sort(nodes: Vec<NodeInfo>) -> Vec<NodeInfo> {
    let mut result: Vec<NodeInfo> = Vec::with_capacity(nodes.len());
    let mut remaining: Vec<NodeInfo> = nodes;

    loop {
        if remaining.is_empty() {
            break;
        }

        let placed_names: std::collections::HashSet<String> = result.iter()
            .map(|n| match &n.namespace {
                Some(ns) => format!("{}::{}", ns, n.name),
                None     => n.name.clone(),
            })
        .collect();

        let mut next: Vec<NodeInfo> = Vec::new();
        let mut placed_any = false;

        for node in remaining {
            let parent_ready = node.parent.as_ref()
                .map(|p| {
                    let pname = match &p.namespace {
                        Some(ns) => format!("{}::{}", ns, p.name),
                        None     => p.name.clone(),
                    };
                    // If the parent has no namespace qualifier, also try the child's namespace
                    // since unqualified parent names in C++ implicitly resolve to the enclosing namespace
                    let pname_in_child_ns = if p.namespace.is_none() {
                        node.namespace.as_ref().map(|ns| format!("{}::{}", ns, p.name))
                    } else {
                        None
                    };
                    placed_names.contains(&pname)
                        || pname_in_child_ns.is_some_and(|n| placed_names.contains(&n))
                })
            .unwrap_or(true);

            if parent_ready {
                result.push(node);
                placed_any = true;
            } else {
                next.push(node);
            }
        }

        remaining = next;

        if !placed_any {
            // Cycle or external parent, append the rest as-is
            result.extend(remaining);
            break;
        }
    }

    result
}

// strips --include-root prefix and normalizes to forward slashes so the path can be used in a #include directive
fn compute_include_path(file: &Path, include_root: Option<&Path>) -> std::string::String {
    if let Some(root) = include_root
        && let Ok(rel) = file.strip_prefix(root) {
            return rel.to_string_lossy().replace('\\', "/");
        }
    file.file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default()
}
