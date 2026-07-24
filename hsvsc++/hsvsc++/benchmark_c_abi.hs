$include <stdio.hsh>

extern "C" benchmark_c_abi_mix(value as u64) -> u64

func main() -> i32 {
    new value[8] as u64 = 0xd6e8feb86659fd93
    new iteration[8] as u64 = 0

    while (iteration < 8000000) {
        value = benchmark_c_abi_mix(value)
        iteration++
    }

    printf("c_abi_checksum_i64=%d\n", value)
    return 0
}
