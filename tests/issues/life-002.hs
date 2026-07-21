func main() -> i32 {
    new p as addr = 0
    if (true) {
        new value[1] as bytes = 'X'
        p = &value
        goto done
    }
    done: return [1]*p
}
