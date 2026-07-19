$include <stdlib.hsh>

func main() -> i32 {
    new value as i8 = -128
    new magnitude as i8 = abs(value)
    return magnitude
}
