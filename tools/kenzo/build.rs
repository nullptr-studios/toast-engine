fn main() {
    // Compile the logging.proto so main.rs can include it
    let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR not set");
    let proto_dir = std::path::Path::new(&manifest_dir)
        .join("..")
        .join("..")
        .join("protos");
    let proto_file = proto_dir.join("logging.proto");

    prost_build::compile_protos(
        &[proto_file.to_str().unwrap()],
        &[proto_dir.to_str().unwrap()],
    )
    .expect("Failed to compile protos");

    println!("cargo:rerun-if-changed={}", proto_file.display());
}
