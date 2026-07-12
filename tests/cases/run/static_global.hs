new global_counter[4]

func main() {
    static counter[4]
    counter = counter + 1
    global_counter = counter + 2
    return global_counter - 3
}
