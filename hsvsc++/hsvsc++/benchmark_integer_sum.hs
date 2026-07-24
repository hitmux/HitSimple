$include <stdio.hsh>

func main() -> i32 {
    new checksum[8] as u64 = 0
    new iteration[8] as u64 = 0

    while (iteration < 16000000) {
        new scaled[8] as u64
        new shifted[8] as u64
        new mixed[8] as u64
        scaled = iteration %8d* 33
        scaled = scaled %8d+ 17
        shifted = iteration? >> 3
        mixed = scaled %8d^ shifted
        checksum = checksum %8d+ mixed
        iteration++
    }

    printf("integer_sum_checksum_i64=%d\n", checksum)
    return 0
}
