$include <stdio.hsh>

func main() -> [4] {
    new count[4]
    new value[4]
    count, value = scanf("%d")
    if (count != 1) {
        return 2
    }
    return value - 42
}
