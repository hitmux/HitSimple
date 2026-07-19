$include <string.hsh>
$include <stdio.hsh>

func main() -> i32 {
    new buffer[4] = 0x03020100
    new index as u64 = get() - 48
    new copied as addr = memcpy(&buffer[index], &buffer[0], 3)
    return 0
}
