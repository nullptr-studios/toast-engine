//! Re-exports for the integration test crate

mod parser;
mod generator;
mod lua_stubs;
mod node;

pub use parser::*;
pub use node::*;
pub use generator::*;
pub use lua_stubs::*;

// tree-sitter sees TOAST_API and __declspec attributes as identifiers that break field parsing
pub fn strip_export_macros(source: &str) -> String {
    source
        .replace("TOAST_API ", "")
        .replace("__declspec(dllexport) ", "")
        .replace("__declspec(dllimport) ", "")
}
