use std::env;
use std::path::PathBuf;

fn main() {
    let proto_dir = env::var("PROTO_PATH").expect("Enviroment variable PROTO_PATH is not defined in .cargo/config.toml");

    let mut proto_file = PathBuf::from(&proto_dir);
    proto_file.push("logging.proto");

    prost_build::compile_protos(
        &[proto_file], // Protos to compile
        &[&proto_dir]  // Directories to search for protos
    ).unwrap_or_else(|e| panic!("Error while compiling protos: {}", e));
}
