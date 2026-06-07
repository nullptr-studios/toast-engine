mod parser;
mod generator;

use parser::*;
use generator::*;

use std::cell::RefCell;
use std::collections::BTreeMap;
use std::env;
use std::fs;
use std::process;
use tree_sitter::{Parser, Query, QueryCursor, StreamingIterator};
use serde::Serialize;

#[derive(Debug, Serialize)]
pub struct VariableInfo {
	#[serde(rename = "type")]
	type_name: String,
	#[serde(skip_serializing_if = "Option::is_none")]
	default: Option<String>,
	attributes: BTreeMap<String, Vec<String>>,
}

#[derive(Debug, Serialize)]
pub struct Class {
	name: String,
	parent: String,
	attributes: BTreeMap<String, Vec<String>>,
	functions: BTreeMap<String, bool>,
	variables: BTreeMap<String, VariableInfo>,
}

thread_local! {
    // 1. Define the global vector wrapped in a RefCell
    static CLASSES: RefCell<Vec<Class>> = const { RefCell::new(Vec::new()) };
	static TICK_FUNCTIONS: [&'static str; 11] = ["preInit", "init", "begin", "earlyTick", "tick", "postPhysics", "lateTick", "end", "destroy", "onEnable", "onDisable"];
}

fn main() {
	let args: Vec<String> = env::args().collect();

	if args.len() != 2 {
		eprintln!("Usage: {} <file_path>", args[0]);
		process::exit(1);
	}

	let file_path: &String = &args[1];

	parse_file(file_path);
	CLASSES.with(|c| dbg!(generate_json(&c.borrow())));
}

/// Parse a C++ file and extract ToastNode classes with Serialize variables.
fn parse_file(file_path: &str) {
	let source_code = fs::read_to_string(file_path).unwrap_or_else(|err| {
		eprintln!("Error reading file '{file_path}': {err}");
		process::exit(1);
	});

	let preprocessed = strip_export_macro(&source_code);

	let mut parser = Parser::new();
	parser
		.set_language(&tree_sitter_cpp::LANGUAGE.into())
		.expect("Language error");

	let tree = parser.parse(&preprocessed, None).unwrap();
	let query = Query::new(
		&tree_sitter_cpp::LANGUAGE.into(),
		"(class_specifier) @class",
	)
		.unwrap();

	let mut cursor = QueryCursor::new();
	let mut captures = cursor.captures(&query, tree.root_node(), preprocessed.as_bytes());

	while let Some((m, idx)) = captures.next() {
		let node = m.captures[*idx].node;
		if let Some(class) = get_class(node, &preprocessed) {
			CLASSES.with_borrow_mut(|c| c.push(class));
		}
	}
}

fn strip_export_macro(source: &str) -> String {
	source
		.replace("TOAST_API ", "")
		.replace("__declspec(dllexport) ", "")
		.replace("__declspec(dllimport) ", "")
}
