template Counter {
    value[4] as i32
}

impl Counter {
    func erase(self as Counter, other as Counter) -> as Counter {
        self.value %d= 0
        other.value %d= 0
        return self
    }
}

func main() {
    new value as Counter
    new other as Counter
    value.value %d= 7
    other.value %d= 9
    value.erase(other)
    if (value.value == 7) {
        if (other.value == 9) {
            return 0
        }
    }
    return 1
}
