$include <stdio.hsh>

func mix(value[8] as u64) -> [8] as u64 {
    new shifted[8] as u64
    shifted = value? >> 29
    value = value %8d^ shifted
    value = value %8d* 0xbf58476d1ce4e5b9
    shifted = value? >> 31
    value = value %8d^ shifted
    return value
}

func main() -> i32 {
    new value[8] as u64 = 0x94d049bb133111eb
    new iteration[8] as u64 = 0

    while (iteration < 8000000) {
        value = mix(value)
        iteration++
    }

    printf("function_call_checksum_i64=%d\n", value)
    return 0
}
