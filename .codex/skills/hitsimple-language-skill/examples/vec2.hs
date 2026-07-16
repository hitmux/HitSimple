$include <math.hsh>
$include <stdio.hsh>

template Vec2 {
    x[8] as f64
    y[8] as f64
}

impl Vec2 {
    op + (lhs as Vec2, rhs as Vec2) -> Vec2 {
        new out as Vec2
        out.x = lhs.x + rhs.x
        out.y = lhs.y + rhs.y
        return out
    }

    func length(self as Vec2) -> f64 {
        return f_sqrt(self.x * self.x + self.y * self.y)
    }
}

func main() -> i32 {
    new a as Vec2
    a.x = 3.0
    a.y = 4.0

    new b as Vec2
    b.x = 1.0
    b.y = 2.0

    new c as Vec2 = a + b
    new length as f64 = c.length()
    print(length)
    return 0
}
