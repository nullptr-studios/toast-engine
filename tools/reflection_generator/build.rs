use std::fs;
use std::path::Path;

fn main() {
    // Get the output directory where the binary will be placed
    let out_dir = std::env::var("OUT_DIR").expect("OUT_DIR not set");
    let target_dir = Path::new(&out_dir)
        .parent()
        .and_then(|p| p.parent())
        .and_then(|p| p.parent())
        .expect("Could not find target directory");

    // Create templates directory next to the binary
    let template_dest = target_dir.join("templates");
    fs::create_dir_all(&template_dest).expect("Failed to create templates directory");

    // Copy templates from source to binary directory
    let template_src = Path::new("templates");
    if template_src.is_dir() {
        for entry in fs::read_dir(template_src).expect("Failed to read templates directory") {
            if let Ok(entry) = entry {
                let path = entry.path();
                if path.is_file() {
                    if let Some(file_name) = path.file_name() {
                        let dest_path = template_dest.join(file_name);
                        fs::copy(&path, &dest_path).expect(&format!(
                            "Failed to copy template {:?}",
                            path
                        ));
                        println!("Copied template: {:?}", dest_path);
                    }
                }
            }
        }
    }

    // Tell cargo to re-run this script if templates change
    println!("cargo:rerun-if-changed=templates/");
}
