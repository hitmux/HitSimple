func fill(target as addr) -> () {
    [1]*target = 'A'
}

func main() -> i32 {
    new raw[1] as cstr
    fill(&raw)
    return raw == "A"
}
