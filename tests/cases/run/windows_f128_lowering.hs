$include <stdlib.hsh>
$include <math.hsh>

func main() {
    new left as f128 = 1.5
    new integer[8] = 2
    new right as f128 = to_f128(integer)
    new sum as f128 = left %f+ right
    new root as f128 = f_sqrt(sum)
    new smaller[1] %b= left < right
    new narrowed as f64 = to_f64(root)
    return 0
}
