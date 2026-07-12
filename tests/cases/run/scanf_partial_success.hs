$include <stdio.hsh>

func main() {
    new count[4]
    new first[4]
    new second[4] = 99
    count, first, second = scanf("%d %d")
    if (count != 1) {
        return 1
    }
    if (first != 7) {
        return 2
    }
    if (second != 99) {
        return 3
    }
    return 0
}
