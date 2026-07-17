extern "C" native_add(left as i32, right as i32) -> i32

func main() {
    if (native_add(19, 23) != 42) {
        return 1
    }
    return 0
}
