$include <stdio.hsh>

func identity(value as u32) -> u32 {
    return value
}

func main() {
    new signed as i32 = 1
    new value as u32 = identity(signed)
    printf("%d\n", value)
    return 0
}
