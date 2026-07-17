extern "C" rust_add(value as i32) -> i32

func main() {
    if (rust_add(18) != 42) {
        return 1
    }
    return 0
}
