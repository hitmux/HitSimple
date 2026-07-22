$include <stdio.hsh>
$include <stdlib.hsh>

func main() {
    new boundary as f16 = 2048.0
    new increment as f16 = 1.0
    new rounded as f16 = boundary %f+ increment
    new result as i32 = to_i32(rounded)
    printf("%d\n", result)
    return result - 2048
}
