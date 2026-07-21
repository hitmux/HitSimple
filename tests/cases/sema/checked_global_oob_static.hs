new small[1] as bytes
new pointer as addr = &small

func inspect() -> i32 {
    return [2]*pointer
}

func main() -> i32 {
    return inspect()
}
