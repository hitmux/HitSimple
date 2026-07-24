$include <stdio.hsh>

func main() -> i32 {
    new state[8] as u64 = 0x123456789abcdef
    new checksum[8] as u64 = 0
    new iteration[8] as u64 = 0

    while (iteration < 12000000) {
        new flag[8] as u64
        state = state %8d* 2862933555777941757
        state = state %8d+ 3037000493
        flag = state %8d& 256
        if (flag == 0) {
            checksum = checksum %8d+ state
        } else {
            checksum = checksum %8d^ state
        }
        iteration++
    }

    printf("branch_checksum_i64=%d\n", checksum)
    return 0
}
