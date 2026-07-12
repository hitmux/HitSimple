$include <stdio.hsh>

template Vec2 {
    x[8] as f64
    y[8] as f64
}

impl Vec2 {
    op format(value as Vec2, out as addr) -> [4] {
        printf("Vec2\n")
        return 5
    }
}

func main() {
    new value as Vec2
    print(value as Vec2)
    return 0
}
