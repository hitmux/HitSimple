$include <stdio.hsh>

func main() {
    new small as u8 = 1
    new wide as u64 = 2
    new result as u64 = true ? small : wide
    printf("%d\n", result)
    return 0
}
