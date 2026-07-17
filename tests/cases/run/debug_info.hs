$include <stdio.hsh>

func helper() {
    new value[4]
    value %d= 41
    printf("%d\n", value)
}

func main() {
    helper()
    return 0
}
