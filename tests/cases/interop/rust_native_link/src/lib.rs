extern "C" {
    fn rust_native_add(value: i32) -> i32;
}

#[no_mangle]
pub extern "C" fn rust_add(value: i32) -> i32 {
    unsafe { rust_native_add(value) }
}
