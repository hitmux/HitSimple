func step() -> () {
    new i[8] as u64 = 0

    while (i < 16) {
        new value[8] as u64 = i
        value = value %8d+ 1
        i++
    }
}

func main() -> i32 {
    new calls[8] as u64 = 0

    while (calls < 100000) {
        step()
        calls++
    }

    return 0
}
