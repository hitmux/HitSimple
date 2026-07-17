use std::env;

fn main() {
    println!(
        "cargo:rustc-link-search=native={}",
        env::var("HITSIMPLE_HSC_LIB_DIR").expect("HITSIMPLE_HSC_LIB_DIR is required")
    );
    println!("cargo:rustc-link-lib=static=hsc");
    println!("cargo:rustc-link-lib=m");
}
