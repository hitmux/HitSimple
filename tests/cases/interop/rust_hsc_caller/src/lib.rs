extern "C" {
    fn hsc_increment(value: i32) -> i32;
}

#[test]
fn cargo_can_call_hitsimple_staticlib() {
    assert_eq!(unsafe { hsc_increment(41) }, 42);
}
