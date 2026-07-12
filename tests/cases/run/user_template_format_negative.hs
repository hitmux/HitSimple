$include <stdio.hsh>

template FailFmt {
    value[4] as i32
}

impl FailFmt {
    op format(value as FailFmt, out as addr) -> [4] {
        return -7
    }
}

func main() {
    new value as FailFmt
    return print(value as FailFmt)
}
