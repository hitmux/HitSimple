$include <stdio.hsh>
$include <stdlib.hsh>

func main() -> i32 {
    new count as i32
    new value as i8
    count, value = scanf("%d")
    if (count != 1) {
        return 1
    }
    new magnitude as i8 = abs(value)
    return magnitude
}
