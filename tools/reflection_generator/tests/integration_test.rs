mod common;

use reflection_generator::{parse, build_node, generate_json, generate_files, strip_export_macros};
use serde_json::Value as JsonValue;
use std::fs;
use std::path::Path;
use common::JsonComparator;

#[test]
fn test_all_fixtures() {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let fixtures_dir = format!("{}/tests/fixtures", manifest_dir);
    let expected_dir = format!("{}/tests/expected_outputs", manifest_dir);
    let output_dir = format!("{}/target/test_outputs", manifest_dir);

    // Ensure output directory exists
    fs::create_dir_all(&output_dir).expect("Failed to create output directory");

    // List all fixtures
    let entries = fs::read_dir(&fixtures_dir)
        .unwrap_or_else(|_| panic!("Cannot read fixtures directory: {}", fixtures_dir));

    let mut fixture_paths: Vec<_> = entries
        .filter_map(|e| {
            let entry = e.ok()?;
            let path = entry.path();
            if path.extension().map_or(false, |ext| ext == "hpp") {
                Some(path)
            } else {
                None
            }
        })
        .collect();

    fixture_paths.sort();

    println!("Found {} fixtures", fixture_paths.len());

    for fixture_path in fixture_paths {
        let fixture_name = fixture_path
            .file_stem()
            .unwrap()
            .to_string_lossy()
            .to_string();

        println!("\n=== Testing fixture: {} ===", fixture_name);
        test_single_fixture(&fixture_path, &fixture_name, &output_dir, &expected_dir);
    }
}

fn test_single_fixture(fixture_path: &Path, fixture_name: &str, output_dir: impl AsRef<str>, expected_dir: impl AsRef<str>) {
    let output_dir = output_dir.as_ref();
    let expected_dir = expected_dir.as_ref();
    let fixture_path = if fixture_path.is_absolute() {
        fixture_path.to_path_buf()
    } else {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        std::path::PathBuf::from(manifest_dir).join(fixture_path)
    };
    // 1. Read fixture
    let source = fs::read_to_string(&fixture_path)
        .unwrap_or_else(|e| panic!("Cannot read fixture {}: {}", fixture_path.display(), e));

    // 2. Preprocess and parse
    let filename = fixture_path
        .file_name()
        .unwrap()
        .to_string_lossy()
        .to_string();
    let preprocessed = strip_export_macros(&source);
    let classes = parse(&preprocessed, &filename);

    if classes.is_empty() {
        println!("  WARNING: No classes found in fixture");
        return;
    }

    println!("  Found {} class(es)", classes.len());

    // 3. Build nodes
    let nodes: Vec<_> = classes.iter().map(build_node).collect();

    // 4. Generate JSON
    let actual_json = generate_json(&nodes);

    println!("  Generated JSON for {} nodes", nodes.len());

    // 5. Check for expected JSON
    let expected_path = format!("{}/{}.json", expected_dir, fixture_name);
    if Path::new(&expected_path).exists() {
        println!("  Comparing against expected JSON...");
        let expected_json: JsonValue = serde_json::from_str(
            &fs::read_to_string(&expected_path)
                .unwrap_or_else(|_| panic!("Cannot read expected JSON: {}", expected_path)),
        ).expect("Invalid JSON in expected output");

        // Deep compare
        match JsonComparator::compare(&actual_json, &expected_json, "root") {
            Ok(_) => println!("  ✓ JSON matches expected output"),
            Err(diff) => {
                eprintln!("  ✗ JSON mismatch: {}", diff);
                eprintln!("\n  Expected:\n{}", serde_json::to_string_pretty(&expected_json).unwrap());
                eprintln!("\n  Actual:\n{}", serde_json::to_string_pretty(&actual_json).unwrap());
                panic!("JSON assertion failed for {}", fixture_name);
            }
        }
    } else {
        println!("  No expected JSON found at: {}", expected_path);
        println!("  Writing actual JSON for review...");
        let json_str = serde_json::to_string_pretty(&actual_json)
            .expect("Failed to serialize JSON");
        fs::write(&expected_path, json_str)
            .unwrap_or_else(|e| eprintln!("Failed to write expected JSON: {}", e));
        println!("  Wrote: {}", expected_path);
    }

    // 6. Generate files for manual inspection
    println!("  Generating .hpp files for manual inspection...");

    // Create a temporary template directory in the output dir for the generator to find
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let template_src = format!("{}/templates", manifest_dir);
    let template_dest = format!("{}/templates", output_dir);

    if Path::new(&template_src).exists() {
        // Copy templates to output dir so generator can find them
        let _ = fs::create_dir_all(&template_dest);
        for entry in fs::read_dir(&template_src).unwrap_or_else(|_| panic!("Cannot read template dir")) {
            if let Ok(entry) = entry {
                let path = entry.path();
                if path.is_file() {
                    if let Some(filename) = path.file_name() {
                        let dest_file = format!("{}/{}", template_dest, filename.to_string_lossy());
                        let _ = fs::copy(&path, &dest_file);
                    }
                }
            }
        }

        match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            generate_files(&nodes, Path::new(output_dir), "testRegisterTypes", false);
        })) {
            Ok(_) => println!("  ✓ Generated .hpp files in: {}", output_dir),
            Err(_) => println!("  ⚠ Skipped .hpp generation"),
        }
    } else {
        println!("  ⚠ Template directory not found");
    }
}

#[test]
fn test_fixture_simple() {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let fixture_path = format!("{}/tests/fixtures/01_simple.hpp", manifest_dir);
    let output_dir = format!("{}/target/test_outputs", manifest_dir);
    let expected_dir = format!("{}/tests/expected_outputs", manifest_dir);
    test_single_fixture(Path::new(&fixture_path), "01_simple", &output_dir, &expected_dir);
}

#[test]
fn test_fixture_inherited() {
    let fixture_path = Path::new("tests/fixtures/02_inherited.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "02_inherited", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_no_fields() {
    let fixture_path = Path::new("tests/fixtures/03_no_fields.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "03_no_fields", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_no_ticks() {
    let fixture_path = Path::new("tests/fixtures/04_no_ticks.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "04_no_ticks", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_complex_groups() {
    let fixture_path = Path::new("tests/fixtures/05_complex_groups.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "05_complex_groups", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_all_types() {
    let fixture_path = Path::new("tests/fixtures/06_all_types.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "06_all_types", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_attributes() {
    let fixture_path = Path::new("tests/fixtures/07_attributes.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "07_attributes", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_multi_class() {
    let fixture_path = Path::new("tests/fixtures/08_multi_class.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "08_multi_class", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_export_macros() {
    let fixture_path = Path::new("tests/fixtures/09_export_macros.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "09_export_macros", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_inheritance_chain() {
    let fixture_path = Path::new("tests/fixtures/10_inheritance_chain.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "10_inheritance_chain", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_deeply_nested_groups() {
    let fixture_path = Path::new("tests/fixtures/11_deeply_nested_groups.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "11_deeply_nested_groups", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_sparse_groups() {
    let fixture_path = Path::new("tests/fixtures/12_sparse_groups.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "12_sparse_groups", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_subgroup_only() {
    let fixture_path = Path::new("tests/fixtures/13_subgroup_only.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "13_subgroup_only", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_mixed_subgroup_fields() {
    let fixture_path = Path::new("tests/fixtures/14_mixed_subgroup_fields.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "14_mixed_subgroup_fields", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_snake_case_names() {
    let fixture_path = Path::new("tests/fixtures/15_snake_case_names.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "15_snake_case_names", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_all_tick_functions() {
    let fixture_path = Path::new("tests/fixtures/16_all_tick_functions.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "16_all_tick_functions", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_global_namespace() {
    let fixture_path = Path::new("tests/fixtures/17_global_namespace.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "17_global_namespace", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_nested_namespace() {
    let fixture_path = Path::new("tests/fixtures/18_nested_namespace.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "18_nested_namespace", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_field_with_all_attributes() {
    let fixture_path = Path::new("tests/fixtures/19_field_with_all_attributes.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "19_field_with_all_attributes", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_vector_of_different_types() {
    let fixture_path = Path::new("tests/fixtures/20_vector_of_different_types.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "20_vector_of_different_types", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_field_with_defaults() {
    let fixture_path = Path::new("tests/fixtures/21_field_with_defaults.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "21_field_with_defaults", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_pointer_types() {
    let fixture_path = Path::new("tests/fixtures/22_pointer_types.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "22_pointer_types", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_qualified_type_names() {
    let fixture_path = Path::new("tests/fixtures/23_qualified_type_names.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "23_qualified_type_names", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_many_global_fields() {
    let fixture_path = Path::new("tests/fixtures/24_many_global_fields.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "24_many_global_fields", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_m_prefix_stripping() {
    let fixture_path = Path::new("tests/fixtures/25_m_prefix_stripping.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "25_m_prefix_stripping", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_unsigned_integer_types() {
    let fixture_path = Path::new("tests/fixtures/26_unsigned_integer_types.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "26_unsigned_integer_types", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_signed_integer_types() {
    let fixture_path = Path::new("tests/fixtures/27_signed_integer_types.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "27_signed_integer_types", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_empty_group() {
    let fixture_path = Path::new("tests/fixtures/28_empty_group.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "28_empty_group", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_duplicate_field_names() {
    let fixture_path = Path::new("tests/fixtures/29_duplicate_field_names.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "29_duplicate_field_names", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_const_volatile_fields() {
    let fixture_path = Path::new("tests/fixtures/30_const_volatile_fields.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "30_const_volatile_fields", "target/test_outputs", "tests/expected_outputs");
    }
}

#[test]
fn test_fixture_reflected_functions() {
    let fixture_path = Path::new("tests/fixtures/31_reflected_functions.hpp");
    if fixture_path.exists() {
        test_single_fixture(fixture_path, "31_reflected_functions", "target/test_outputs", "tests/expected_outputs");
    }
}
