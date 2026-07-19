$include <stdio.hsh>

func main() -> i32 {
    new converted as i32
    new count as u64
    converted, count = scanf("%u")
    if (converted != 1) {
        return 1
    }
    new file as handle = fopen("/dev/null", "r")
    new buffer[1]
    new read as u64 = fread(buffer, 2, count, file)
    return 0
}
