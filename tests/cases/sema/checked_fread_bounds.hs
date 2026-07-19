$include <stdio.hsh>

func main() -> i32 {
    new file as handle = fopen("/dev/null", "r")
    new destination[1] as bytes
    new count as u64 = fread(destination, 1, 2, file)
    return 0
}
