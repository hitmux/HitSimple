func main() {
    try {
        throw "hi"
    } catch (literal[4] as cstr) {
        if ((literal as i32) != 0x00006968) {
            return 1
        }
    }

    new source[3] as cstr = "ok"
    try {
        throw source
    } catch (copied[4] as cstr) {
        return (copied as i32) - 0x00006b6f
    }

    return 2
}
