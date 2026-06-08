mod parser;
mod generator;

pub use parser::*;
use generator::*;

use std::env;
use std::fs;
use std::process;

fn main() {
	let args: Vec<String> = env::args().collect();

	if args.len() != 2 {
		eprintln!("Usage: {} <file_path>", args[0]);
		process::exit(1);
	}

	let file_path = &args[1];
	let source = fs::read_to_string(file_path).unwrap_or_else(|err| {
		eprintln!("Error reading file '{file_path}': {err}");
		process::exit(1);
	});

	let preprocessed = strip_export_macro(&source);
	let classes = parse(&preprocessed);
	println!("{}", serde_json::to_string_pretty(&generate_json(&classes)).unwrap());
}

fn strip_export_macro(source: &str) -> String {
	source
		.replace("TOAST_API ", "")
		.replace("__declspec(dllexport) ", "")
		.replace("__declspec(dllimport) ", "")
}
