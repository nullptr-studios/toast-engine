#[no_mangle]
pub extern "C" fn toast_rust_tick() {
    println!("hi from rust");
}
