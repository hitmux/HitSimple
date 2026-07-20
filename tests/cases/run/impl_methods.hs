$include <stdlib.hsh>

template Counter {
    value[4] as i32
}

impl Counter {
    func next(self as Counter) -> as Counter {
        new result as Counter
        result.value %d= self.value + 1
        return result
    }

    func add(self as Counter, amount as i32) -> as Counter {
        new result as Counter
        result.value %d= self.value + amount
        return result
    }

    func add(self as Counter, amount as i16) -> as Counter {
        new result as Counter
        result.value %d= self.value + to_i32(amount)
        return result
    }
}

func main() {
    new value as Counter
    value.value %d= 10
    value.next()
    value = value.next()
    new twenty as i32 = 20
    new three as i16 = 3
    value = value.add(twenty)
    value = value.add(three)
    if (value.value == 34) {
        return 0
    }
    return 1
}
