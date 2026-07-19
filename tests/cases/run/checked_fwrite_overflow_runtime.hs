$include <stdio.hsh>

func main() -> i32 {
    new converted as i32
    new count as u64
    converted, count = scanf("%u")
    if (converted != 1) {
        return 1
    }
    new file as handle = fopen("/dev/null", "w")
    new buffer[1]
    new written as u64 = fwrite(buffer, 2, count, file)
    return 0
}
