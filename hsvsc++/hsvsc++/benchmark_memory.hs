$include <stdio.hsh>
$include <stdlib.hsh>

func memory_checksum(elements[8] as u64, rounds[8] as u64) -> [8] as u64 {
    new total_bytes[8] as u64
    total_bytes = elements %8d* 8
    new data as addr = alloc(total_bytes)

    if (!data) {
        return 0
    }

    new index[8] as u64 = 0
    while (index < elements) {
        new offset[8] as u64
        new item as addr
        new seeded[8] as u64
        offset = index %8d* 8
        item = data? + offset?
        seeded = index %8d* 1664525
        seeded = seeded %8d+ 1013904223
        [8]*item as u64 = seeded
        index++
    }

    new round[8] as u64 = 0
    while (round < rounds) {
        index = 0
        while (index < elements) {
            new offset[8] as u64
            new item as addr
            new value[8] as u64
            new shifted[8] as u64
            new mixed[8] as u64
            new product[8] as u64
            offset = index %8d* 8
            item = data? + offset?
            value = [8]*item as u64
            shifted = value? >> 17
            mixed = value %8d^ shifted
            product = mixed %8d* 1103515245
            value = product %8d+ 12345
            [8]*item as u64 = value
            index++
        }
        round++
    }

    new checksum[8] as u64 = 0
    index = 0
    while (index < elements) {
        new offset[8] as u64
        new item as addr
        new value[8] as u64
        new index_hash[8] as u64
        new mixed[8] as u64
        offset = index %8d* 8
        item = data? + offset?
        value = [8]*item as u64
        index_hash = index %8d* 2654435761
        mixed = value %8d^ index_hash
        checksum = checksum %8d+ mixed
        index++
    }

    free(data)
    return checksum
}

func main() -> i32 {
    new checksum[8] as u64 = memory_checksum(16 %8d* 1024 %8d* 1024, 56)
    printf("memory_checksum_i64=%d\n", checksum)
    return 0
}
