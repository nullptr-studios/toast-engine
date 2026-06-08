mod parser;
mod generator;

pub use parser::{parse, Attribute, Parent, Field, Class};
pub use generator::{NodeInfo, build_node, generate_json, generate_files};

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
		let classes = parse(&preprocessed);

		for class in &classes {
			all_nodes.push(build_node(class, &include_path));
		}
	}

	// Write JSON database
	if let Some(parent) = cli.database.parent() {
		if !parent.as_os_str().is_empty() {
			fs::create_dir_all(parent)
				.unwrap_or_else(|e| eprintln!("warning: cannot create database directory: {e}"));
		}
	}
	let json = serde_json::to_string_pretty(&generate_json(&all_nodes))
		.expect("JSON serialisation failed");
	fs::write(&cli.database, json)
		.unwrap_or_else(|e| eprintln!("warning: cannot write database '{}': {e}", cli.database.display()));

	// Generate files
	generate_files(&all_nodes, &cli.output, &cli.register_fn);

	println!(
		"refgen: generated {} type(s) → '{}' (fn: {})",
		all_nodes.len(),
		cli.output.display(),
		cli.register_fn
	);
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
			if let Ok(content) = fs::read_to_string(&path) {
				if content.contains("[[ToastNode]]") {
					result.push(path);
				}
			}
		}
	}
	result
}

/// Strip --include-root prefix and normalize to forward slashes
fn compute_include_path(file: &Path, include_root: Option<&Path>) -> std::string::String {
	if let Some(root) = include_root {
		if let Ok(rel) = file.strip_prefix(root) {
			return rel.to_string_lossy().replace('\\', "/");
		}
	}
	file.file_name()
		.map(|n| n.to_string_lossy().into_owned())
		.unwrap_or_default()
}

pub fn strip_export_macros(source: &str) -> std::string::String {
	source
		.replace("TOAST_API ", "")
		.replace("__declspec(dllexport) ", "")
		.replace("__declspec(dllimport) ", "")
}
