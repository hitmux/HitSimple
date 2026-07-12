$include <stdio.hsh>

struct Pair {
    left[4]
    right[4]
}

func main() {
    new p[s2] ;Pair
    p.left = 3
    p[s1].right = sizeof(Pair)
    printf("%d\n", p[s1].right)
    return 0
}
