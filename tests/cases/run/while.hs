$include <stdio.hsh>

func main() {
    new x[1]
    new y[1]
    x %d= 0
    y %d= 6
    while (y) {
        x %d= x %d+ 7
        y %d= y %d- 1
        if (y) {
            continue
        } else {
            break
        }
    }
    printf("%d\n", x)
    return 0
}
