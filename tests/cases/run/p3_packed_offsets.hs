$include <stdio.hsh>

template OffsetOne {
    padding[1] as u8
    value[4] as u32
}

template OffsetTwo {
    padding[2]
    value[4] as u32
}

template OffsetThree {
    padding[3]
    value[4] as u32
}

func main() {
    new one as OffsetOne
    new two as OffsetTwo
    new three as OffsetThree
    one.value = 10
    two.value = 20
    three.value = 30
    printf("%d\n", one.value + two.value + three.value)
    return 0
}
