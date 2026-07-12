func main() {
    try {
        try {
            throw 1
        } catch (inner[1]) {
            throw 2
        }
    } catch (outer[1]) {
        return outer - 2
    }
    return 1
}
