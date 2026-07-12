$include <stdlib.hsh>

func main() {
    try {
        try {
            throw 1.5
        } catch (inner as f64) {
            new adjusted as f64 = inner %f+ 1.0
            throw adjusted
        }
    } catch (outer as f64) {
        return to_i32(outer) - 2
    }
    return 1
}
