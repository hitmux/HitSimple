func initial() -> addr {
    return 0
}

func main() -> i32 {
    new p as addr = initial()
    while (true) {
        new value[1] as bytes = 'X'
        p = &value
        break
    }
    return [1]*p
}
