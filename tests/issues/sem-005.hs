$include <stdio.hsh>

func main() {
    new wrapped as u64 = 18446744073709551615 + 1
    printf("%d\n", wrapped)
    return 0
}
