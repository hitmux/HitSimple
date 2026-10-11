extern host_buffer[1] as bytes

func main() -> i32 {
    new address as addr = &host_buffer
    return [1]*address
}
