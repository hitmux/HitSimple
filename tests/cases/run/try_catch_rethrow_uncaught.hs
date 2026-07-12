func main() {
    try {
        throw 1
    } catch (inner[1]) {
        throw 2
    }
    return 0
}
