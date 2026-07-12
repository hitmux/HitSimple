template ErrorView {
    code[4] as i32
    detail[4] as i32
}

func main() {
    new value as ErrorView
    value.code = 40
    value.detail = 2
    try {
        throw value
    } catch (error as ErrorView) {
        return error.code + error.detail - 42
    }
    return 1
}
