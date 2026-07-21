func condition() -> bool {
    return true
}

func main() -> i32 {
    new p as addr = 0
    if (condition()) {
        new value[1] as bytes = 'X'
        p = &value
        goto done
    }
    done: return [1]*p
}
