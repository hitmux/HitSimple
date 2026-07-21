func main() -> i32 {
    new small[1] as bytes
    new large[8] as bytes
    new p as addr = &small
    goto done
    p = &large
    done: return [2]*p
}
