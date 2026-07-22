$include <stdio.hsh>

func main() {
    new one as f128 = 1.0
    new increment as f128 = 0.00000000000000000001
    new result as f128 = one %f+ increment
    if (result > one) {
        printf("f128\n")
        return 0
    }
    return 1
}
