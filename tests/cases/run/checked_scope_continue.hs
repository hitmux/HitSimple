func main() -> i32 {
    new count as i32 = 0
    while (count < 1) {
        new value[1] as bytes = 'X'
        count++
        continue
    }
    return 0
}
