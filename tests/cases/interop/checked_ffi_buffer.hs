extern "C" native_buffer() -> addr

func main() -> i32 {
    new address as addr = native_buffer()
    return [1]*address
}
