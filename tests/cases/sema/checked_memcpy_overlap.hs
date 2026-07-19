$include <string.hsh>

func main() -> i32 {
    new buffer[4] = 0x03020100
    new copied as addr = memcpy(&buffer[1], &buffer[0], 3)
    return 0
}
