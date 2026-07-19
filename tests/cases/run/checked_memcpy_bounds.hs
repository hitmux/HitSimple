$include <string.hsh>
$include <stdio.hsh>

func main() -> i32 {
    new source[4] = 0x03020100
    new destination[1]
    new count as u64 = get() - 48
    new copied as addr = memcpy(destination, source, count)
    return 0
}
