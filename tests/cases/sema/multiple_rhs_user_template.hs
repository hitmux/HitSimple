template First { value[4] as i32; }
template Second { value[4] as i32; }
func main() -> i32 {
    new first as First
    new second as First
    new source as Second
    first, second = first, source
    return 0
}
