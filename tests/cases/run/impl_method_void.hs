template Counter {
    value[4] as i32
}

impl Counter {
    func touch(self as Counter) {
    }
}

func main() {
    new value as Counter
    value.touch()
    return 0
}
