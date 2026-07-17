#[no_mangle]
pub extern "C" fn rust_add(value: i32) -> i32 {
    value + 24
}
