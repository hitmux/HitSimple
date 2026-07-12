$include <stdlib.hsh>
$include <stdio.hsh>

func main() {
    new ptr = calloc(1, 8)
    [1]*ptr = 'h'
    [1]*(ptr? + 1) = 'i'
    [1]*(ptr? + 2) = 0
    printf("%s\n", ptr)
    free(ptr)
    return 0
}
