$include <stdio.hsh>

func hot(value[4]) -> [4] {
    if (value <= 0) {
        return 0
    }
    return hot(value - 1) + 1
}

func main() {
    new total[4] = 0
    for (new i[4] = 0; i < 1000; i++) {
        total = total + hot(i % 17)
    }
    printf("%d\n", total)
    return 0
}
