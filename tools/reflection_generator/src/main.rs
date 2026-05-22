// main.rs
#![allow(clippy::only_used_in_recursion)]
#![allow(unused)]

mod parser;

use parser::*;

use std::cell::RefCell;
use std::env;
use std::fs;
use std::process;
use tree_sitter::Parser;
use tree_sitter::Query;
use tree_sitter::QueryCursor;
use tree_sitter::StreamingIterator;

#[derive(Debug)]
pub struct Attribute {
    name: String,
    arguments: Option<Vec<String>>,
}

#[derive(Debug)]
pub struct Function {
    name: String,
    attributes: Option<Vec<Attribute>>,
}

#[derive(Debug)]
pub struct Variable {
    name: String,
    prefix: String,
    attributes: Option<Vec<Attribute>>,
}

#[derive(Debug)]
pub struct Class {
    name: String,
    functions: Vec<Function>,
    variable: Vec<Variable>,
    attributes: Option<Vec<Attribute>>,
}

thread_local! {
    // 1. Define the global vector wrapped in a RefCell
    static CLASSES: RefCell<Vec<Class>> = const { RefCell::new(Vec::new()) };
}

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() != 2 {
        eprintln!("Usage: {} <file_path>", args[0]);
        process::exit(1);
    }

    let file_path: &String = &args[1];

    parse_file(file_path);
}

fn parse_file(file_path: &str) {
    let source_code = fs::read_to_string(file_path).unwrap_or_else(|err| {
        eprintln!("Error reading file '{file_path}': {err}");
        process::exit(1);
    });

    let mut parser = Parser::new();
    parser
        .set_language(&tree_sitter_cpp::LANGUAGE.into())
        .expect("Language error");

    let tree = parser.parse(&source_code, None).unwrap();
    let query = Query::new(
        &tree_sitter_cpp::LANGUAGE.into(),
        "(class_specifier) @class",
    )
    .unwrap();

    let mut cursor = QueryCursor::new();
    let mut captures = cursor.captures(&query, tree.root_node(), source_code.as_bytes());

    while let Some((m, idx)) = captures.next() {
        let node = m.captures[*idx].node;
        if let Some(class) = get_class(node, &source_code) {
            CLASSES.with_borrow_mut(|c| c.push(class));
        }
    }
}
