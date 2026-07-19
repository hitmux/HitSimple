$include <stdio.hsh>

func main() -> i32 {
    new file as handle = fopen("/dev/null", "r")
    new destination[1] as bytes
    new requested as u64 = get() - 48
    new count as u64 = fread(destination, 1, requested, file)
    return 0
}
