extern "C" cpp_add_suffix(value as i32) -> i32

func main() {
    if (cpp_add_suffix(18) != 42) {
        return 1
    }
    return 0
}
