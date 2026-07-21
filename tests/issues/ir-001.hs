template Packed {
    marker[1] as u8
    value[8] as u64
}

func main() {
    new packed as Packed
    packed.value = 1
    new copied as u64 = packed.value
    if (copied) {
        return 0
    }
    return 1
}
