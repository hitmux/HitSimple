$include <stdlib.hsh>

func main() {
    new value as f64 = 42.5
    try {
        throw value
    } catch (error as f64) {
        return to_i32(error) - 42
    }
    return 1
}
