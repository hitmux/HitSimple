template Pair {
    left[4] as i32
    right[4] as i32
}

func main() {
    new pair as Pair
    pair.left = 40
    pair.right = 2
    return pair.left + pair.right - 42
}
