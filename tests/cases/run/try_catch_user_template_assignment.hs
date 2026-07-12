template Counter {
    value[4] as i32
}

impl Counter {
    op = (dst as Counter, src as Counter) -> [4] {
        dst.value = src.value + 1
        return dst
    }
}

func main() {
    new source as Counter
    source.value = 41
    try {
        throw source
    } catch (error as Counter) {
        return error.value - 42
    }
    return 1
}
