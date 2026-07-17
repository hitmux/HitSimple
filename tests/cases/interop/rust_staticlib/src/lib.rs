#[no_mangle]
pub extern "C" fn rust_add(value: i32) -> i32 {
    if cfg!(feature = "bonus") {
        value + 24
    } else {
        value + 1
    }
}
