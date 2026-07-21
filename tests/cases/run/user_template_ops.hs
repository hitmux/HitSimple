template Vec2 {
    x[8] as f64
    y[8] as f64
}

impl Vec2 {
    op + (lhs as Vec2, rhs as Vec2) -> [16] {
        new result as Vec2
        result.x %f= lhs.x %f+ rhs.x
        result.y %f= lhs.y %f+ rhs.y
        return result
    }

    op * (lhs as Vec2, scalar as f64) -> [16] {
        new result as Vec2
        result.x %f= lhs.x %f* scalar
        result.y %f= lhs.y %f* scalar
        return result
    }

    op == (lhs as Vec2, rhs as Vec2) -> [1] {
        return lhs.x == rhs.x && lhs.y == rhs.y
    }

    op = (dst as Vec2, src as Vec2) -> [16] {
        dst.x %f= src.x %f+ 1.0
        dst.y %f= src.y %f+ 1.0
        return dst
    }
}

func main() {
    new lhs as Vec2
    lhs.x %f= 1.0
    lhs.y %f= 2.0
    new rhs as Vec2
    rhs.x %f= 3.0
    rhs.y %f= 4.0
    new scale as f64 = 2.0
    new sum as Vec2 = lhs + rhs
    new scaled as Vec2 = sum * scale
    new copied as Vec2
    copied = scaled
    new expected as Vec2
    expected.x %f= 12.0
    expected.y %f= 16.0
    if (copied == expected) {
        return 0
    }
    return 1
}
