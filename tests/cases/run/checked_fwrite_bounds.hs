$include <stdio.hsh>

func main() -> i32 {
    new file as handle = fopen("/dev/null", "w")
    new source[1] as bytes
    new requested as u64 = get() - 48
    new count as u64 = fwrite(source, 1, requested, file)
    return 0
}
