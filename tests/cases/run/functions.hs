$include <stdio.hsh>

func add_one(value[4]) -> [4] {
    return value + 1
}

func pair(value[4]) -> ([4], [4]) {
    return value, value + 1
}

func main() {
    new a[4], b[4]
    a = add_one(40)
    a, b = pair(a)
    printf("%d\n", b)
    return 0
}
