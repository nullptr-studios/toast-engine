//! Re-exports for the integration test crate

mod parser;
mod generator;

pub use parser::{parse, Attribute, Parent, Field, Class};
pub use generator::{NodeInfo, build_node, generate_json, generate_files, validate_class};

// tree-sitter sees TOAST_API and __declspec attributes as identifiers that break field parsing
pub fn strip_export_macros(source: &str) -> String {
	source
		.replace("TOAST_API ", "")
		.replace("__declspec(dllexport) ", "")
		.replace("__declspec(dllimport) ", "")
}
