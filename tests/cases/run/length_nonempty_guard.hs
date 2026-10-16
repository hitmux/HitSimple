// Exercise the runtime guard directly, independent of zero-sized temporary
// registration, whose separate existing restriction is documented.
extern "C" hs_check_view_nonempty(value as u64) -> ()
func main() -> i32 {
    hs_check_view_nonempty(0)
    return 0
}
