$include <stdio.hsh>
$include <stdlib.hsh>

func main() -> i32 {
    new converted as i32
    new value as f128
    converted, value = scanf("%16f")
    if (converted != 1) {
        return 1
    }
    new integer as i32 = to_i32(value)
    if (integer != 1) {
        return 2
    }
    new written as i32 = printf("%16f\n", value)
    if (written == 0) {
        return 3
    }
    return 0
}
