$include <stdio.hsh>
$include <stdlib.hsh>

func main() -> i32 {
    new data[64] as bytes
    new base as addr = &data
    new checksum[8] as u64 = 0
    new iteration[8] as u64 = 0

    while (iteration < 100000) {
        new slot[8] as u64
        new item as addr
        new written[1] as u8
        new value[1] as u8
        slot = iteration %8d& 63
        item = base? + slot?
        written = to_u8(iteration)
        [1]*item = written
        value = [1]*item
        checksum = checksum %8d+ value
        iteration++
    }

    printf("checked_loop_checksum_i64=%d\n", checksum)
    return 0
}
