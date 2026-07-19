$include <stdio.hsh>
$include <stdlib.hsh>

func main() -> i32 {
    new converted as i32
    new count as u64
    converted, count = scanf("%u")
    if (converted != 1) {
        return 1
    }
    new ptr as addr = calloc(count, 2)
    return 0
}
