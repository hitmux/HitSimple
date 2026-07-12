$include <math.hsh>
$include <stdlib.hsh>

func main() {
    new half as f16 = 1.5
    new half_result as f16 = f_sqrt(half)
    new quad as f128 = 1.5
    new quad_result as f128 = f_sin(quad)
    new half_integer[4] = to_i32(half_result)
    new quad_integer[4] = to_i32(quad_result)
    if (half_integer != 1) {
        return 1
    }
    if (quad_integer != 0) {
        return 2
    }
    return 0
}
