use crate::Class;
use serde_json::to_value;
use minijinja::{Environment, context};
use std::fs;

pub fn generate_json(classes: &[Class]) -> serde_json::Value {
	to_value(classes).unwrap_or_else(|_| serde_json::Value::Array(vec![]))
}

pub fn generate_files(classes: &[Class]) {
	let mut env = Environment::new();
}
