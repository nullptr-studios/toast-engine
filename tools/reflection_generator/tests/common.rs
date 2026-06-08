use serde_json::{json, Value as JsonValue};
use std::fmt;

/// Deep JSON comparison with path tracking for better error messages
pub struct JsonComparator;

#[derive(Debug)]
pub struct JsonDiff {
    pub path: String,
    pub reason: String,
}

impl fmt::Display for JsonDiff {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: {}", self.path, self.reason)
    }
}

impl JsonComparator {
    /// Compare two JSON values and return detailed differences
    pub fn compare(actual: &JsonValue, expected: &JsonValue, path: &str) -> Result<(), JsonDiff> {
        match (actual, expected) {
            (JsonValue::Null, JsonValue::Null) => Ok(()),
            (JsonValue::Bool(a), JsonValue::Bool(e)) if a == e => Ok(()),
            (JsonValue::Bool(a), JsonValue::Bool(e)) => {
                Err(JsonDiff {
                    path: path.to_string(),
                    reason: format!("bool mismatch: {} != {}", a, e),
                })
            }
            (JsonValue::Number(a), JsonValue::Number(e)) if a == e => Ok(()),
            (JsonValue::Number(a), JsonValue::Number(e)) => {
                Err(JsonDiff {
                    path: path.to_string(),
                    reason: format!("number mismatch: {} != {}", a, e),
                })
            }
            (JsonValue::String(a), JsonValue::String(e)) if a == e => Ok(()),
            (JsonValue::String(a), JsonValue::String(e)) => {
                Err(JsonDiff {
                    path: path.to_string(),
                    reason: format!("string mismatch: '{}' != '{}'", a, e),
                })
            }
            (JsonValue::Array(a), JsonValue::Array(e)) => {
                if a.len() != e.len() {
                    return Err(JsonDiff {
                        path: path.to_string(),
                        reason: format!("array length mismatch: {} != {}", a.len(), e.len()),
                    });
                }
                for (i, (av, ev)) in a.iter().zip(e.iter()).enumerate() {
                    Self::compare(av, ev, &format!("{}[{}]", path, i))?;
                }
                Ok(())
            }
            (JsonValue::Object(a), JsonValue::Object(e)) => {
                // Check all expected keys exist and match
                for key in e.keys() {
                    let av = a.get(key).ok_or_else(|| JsonDiff {
                        path: format!("{}.{}", path, key),
                        reason: "missing key in actual".to_string(),
                    })?;
                    let ev = &e[key];
                    Self::compare(av, ev, &format!("{}.{}", path, key))?;
                }
                // Check no extra keys in actual
                for key in a.keys() {
                    if !e.contains_key(key) {
                        return Err(JsonDiff {
                            path: format!("{}.{}", path, key),
                            reason: "unexpected extra key in actual".to_string(),
                        });
                    }
                }
                Ok(())
            }
            _ => Err(JsonDiff {
                path: path.to_string(),
                reason: format!("type mismatch: {} vs {}", json_type_str(actual), json_type_str(expected)),
            }),
        }
    }
}

fn json_type_str(v: &JsonValue) -> &'static str {
    match v {
        JsonValue::Null => "null",
        JsonValue::Bool(_) => "bool",
        JsonValue::Number(_) => "number",
        JsonValue::String(_) => "string",
        JsonValue::Array(_) => "array",
        JsonValue::Object(_) => "object",
    }
}

/// Helper to generate a minimal JSON for a single class NodeInfo
pub fn minimal_node_json(name: &str, namespace: Option<&str>) -> JsonValue {
    json!({
        "name": name,
        "namespace": namespace,
        "parent": null,
        "source_file": "test.hpp",
        "attributes": {},
        "functions": {
            "pre_init": false,
            "init": false,
            "begin": false,
            "early_tick": false,
            "tick": false,
            "post_physics": false,
            "late_tick": false,
            "end": false,
            "destroy": false,
            "on_enable": false,
            "on_disable": false,
            "load": false,
            "save": false,
        },
        "global_fields": [],
        "groups": []
    })
}
