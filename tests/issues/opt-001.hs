$include <stdlib.hsh>

func main() {
    new values[1024]
    new index as u64 = 0

    while (index < 1024) {
        new pointer as addr = &values[index]
        [1]*pointer = index?
        index++
    }

    index = 0
    while (index < 1024) {
        new pointer as addr = &values[index]
        [1]*pointer = [1]*pointer + 17
        index++
    }

    new checksum as u64 = 0
    index = 0
    while (index < 1024) {
        new pointer as addr = &values[index]
        new value as u8 = [1]*pointer
        checksum = checksum %8d+ value
        index++
    }

    return to_i32(checksum)
}
