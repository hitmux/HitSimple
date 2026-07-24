$include <stdio.hsh>

func main() -> i32 {
    new value[8] as u64 = 0x9e3779b97f4a7c15
    new iteration[8] as u64 = 0

    while (iteration < 8000000) {
        new shifted[8] as u64
        shifted = value? >> 13
        value = value %8d^ shifted
        shifted = value? >> 7
        value = value %8d^ shifted
        shifted = value? >> 17
        value = value %8d^ shifted
        value = value %8d* 6364136223846793005
        value = value %8d+ 1442695040888963407
        iteration++
    }

    printf("bitops_checksum_i64=%d\n", value)
    return 0
}
