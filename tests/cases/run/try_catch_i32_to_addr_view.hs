func main() {
    new source as i32 = 7
    try {
        throw source
    } catch (error as addr) {
        return error? - 7
    }
    return 1
}
