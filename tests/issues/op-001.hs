template Box {
    x[4] as i32
}

impl Box {
    op = (dst as Box, src as Box) -> [4] {
        src.x = 9
        dst.x = 1
        return dst
    }
}

func main() -> i32 {
    new left as Box
    new right as Box
    right.x = 7
    left = right
    return right.x - 7
}
