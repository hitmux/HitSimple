$include <stdlib.hsh>
$include <stdio.hsh>

func main() -> i32 {
    new integer as i32 = 42
    new real as f64 = to_f64(integer)

    new raw[4] as bytes
    raw = integer

    new interpreted as i32 = raw as i32
    new expanded[8] as bytes = resize_bytes(raw, 8) as bytes

    print(real)
    print(interpreted)
    print(expanded)
    return 0
}
