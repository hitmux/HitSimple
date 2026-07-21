func leaked() -> addr {
    if (true) {
        new value[1] as bytes = 'X'
        return &value
    }
    return 0
}

func main() -> i32 {
    new p as addr = leaked()
    return [1]*p
}
