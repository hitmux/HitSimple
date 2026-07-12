template ErrorView {
    code[4] as i32
}

func make_error() -> as ErrorView {
    new error as ErrorView
    error.code = 42
    return error
}

func main() {
    try {
        throw make_error()
    } catch (error as ErrorView) {
        return error.code - 42
    }
    return 1
}
