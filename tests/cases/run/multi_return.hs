func pair(value[4]) -> ([4], [4]) {
    return value, value + 1
}

func main() {
    new a[4], b[4]
    a, b = pair(40)
    return b - 41
}
