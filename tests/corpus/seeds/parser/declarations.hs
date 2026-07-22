template Pair {
    left[4] as i32
    right[4] as i32
}

struct RawPair {
    left[4]
    right[4]
}

extern host_value() -> i32
extern errno as i32

impl Pair {
    op == (left as Pair, right as Pair) -> [1] {
        return left.left == right.left && left.right == right.right
    }
}

func main() {
    new pair as Pair
    return 0
}
