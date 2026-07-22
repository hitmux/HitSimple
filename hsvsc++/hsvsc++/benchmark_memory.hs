$include <stdio.hsh>
$include <stdlib.hsh>

func memory_checksum(elements[8] as u64, rounds[8] as u64) -> [8] as u64 {
    new total_bytes[8] as u64
    total_bytes = elements %8d* 4
    new data as addr = alloc(total_bytes)

    if (!data) {
        return 0
    }

    new index[8] as u64 = 0
    while (index < elements) {
        new offset[8] as u64
        new item as addr
        new seeded[4] as u32
        offset = index %8d* 4
        item = data? + offset?
        seeded = index %4d* 1664525
        seeded = seeded %4d+ 1013904223
        [4]*item = seeded
        index++
    }

    new round[8] as u64 = 0
    while (round < rounds) {
        index = 0
        while (index < elements) {
            new offset[8] as u64
            new item as addr
            new value[4] as u32
            new shifted[4] as u32
            new mixed[4] as u32
            new product[4] as u32
            offset = index %8d* 4
            item = data? + offset?
            value = [4]*item
            shifted = value? >> 17
            mixed = value %4d^ shifted
            product = mixed %4d* 1103515245
            value = product %4d+ 12345
            [4]*item = value
            index++
        }
        round++
    }

    new checksum[8] as u64 = 0
    index = 0
    while (index < elements) {
        new offset[8] as u64
        new item as addr
        new value[4] as u32
        new index_hash[4] as u32
        new mixed[4] as u32
        offset = index %8d* 4
        item = data? + offset?
        value = [4]*item
        index_hash = index %4d* 2654435761
        mixed = value %4d^ index_hash
        checksum = checksum %8d+ mixed
        index++
    }

    free(data)
    return checksum
}

func main() -> i32 {
    new checksum[8] as u64 = memory_checksum(32 %8d* 1024 %8d* 1024, 56)
    printf("memory_checksum_i64=%d\n", checksum)
    return 0
}
