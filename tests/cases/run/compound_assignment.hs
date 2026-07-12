$include <stdio.hsh>

func main() {
    new x[1]
    x %d= 5
    x %d+= 10
    x %d*= 3
    x %d-= 3
    x %d/= 1
    x %d%= 100
    printf("%d\n", x)
    return 0
}
