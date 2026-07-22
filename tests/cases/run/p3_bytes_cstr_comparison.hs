$include <stdio.hsh>

func main() {
    new left[3] as bytes
    new equal[3] as bytes
    new greater[3] as bytes
    left[0] = 'a'
    left[1] = 'b'
    left[2] = 'c'
    equal[0] = 'a'
    equal[1] = 'b'
    equal[2] = 'c'
    greater[0] = 'a'
    greater[1] = 'b'
    greater[2] = 'd'
    new text[3] as cstr = "ok"
    if (left == equal && left < greater && text == "ok") {
        printf("comparison\n")
        return 0
    }
    return 1
}
