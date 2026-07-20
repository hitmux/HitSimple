$include <stdio.hsh>

func main() {
    new source as u32 = 4294967295
    new destination as u64 = source
    printf("%d\n", destination)
    return 0
}
