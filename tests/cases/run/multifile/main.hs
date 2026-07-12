$include <stdio.hsh>

extern add_two(value[4]) -> [4]

func main() {
    new value[4]
    value = add_two(40)
    printf("%d\n", value)
    return 0
}
