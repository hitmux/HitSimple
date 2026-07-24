$include <stdio.hsh>
$include <stdlib.hsh>

func main() -> i32 {
    new checksum[8] as i64 = 0
    new iteration[8] as u64 = 0

    while (iteration < 12000000) {
        new bounded[8] as u64
        new value[8] as i64
        new magnitude[8] as i64
        bounded = iteration %8d& 8191
        value = to_i64(bounded)
        value = value %8d- 4096
        magnitude = abs(value)
        checksum = checksum %8d+ magnitude
        iteration++
    }

    printf("stdlib_checksum_i64=%d\n", checksum)
    return 0
}
