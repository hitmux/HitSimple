use std::env;

fn main() {
    println!("cargo:rerun-if-env-changed=HITSIMPLE_RUST_NATIVE_LIB_DIR");
    println!(
        "cargo:rustc-link-search=native={}",
        env::var("HITSIMPLE_RUST_NATIVE_LIB_DIR")
            .expect("HITSIMPLE_RUST_NATIVE_LIB_DIR is required")
    );
    println!("cargo:rustc-link-lib=static=rust_native_helper");
}
