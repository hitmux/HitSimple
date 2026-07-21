$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>
$include <math.hsh>
$include <ctype.hsh>
$include <time.hsh>
$include <assert.hsh>

$define PROJECT_SEED 40
$define EXPECTED_LABEL "HitSimple"
$define CHECK_ENABLED 1

$ifdef CHECK_ENABLED
new global_runs[4]
new global_status[4]
$else
new global_disabled[4]
$endif

struct Metric {
    count[4]
    total[4]
    tag[16]
}

struct Pair {
    left[4]
    right[4]
}

struct BufferView {
    ptr[P]
    len[4]
}

template Vec2 {
    x[8] as f64
    y[8] as f64
}

impl Vec2 {
    op format(value as Vec2, out as addr) -> [4] {
        return 0
    }
}

func make_pair(seed[4]) -> ([4], [4]) {
    return seed, seed + 1
}

func divmod(value[4], divisor[4]) -> (quot[4] as i32, rem[4] as i32) {
    return value / divisor, value % divisor
}

func fold_series(limit[4]) -> [4] {
    new sum[4] = 0
    for (new i[4] = 0; i < limit; i++) {
        if (i == 3) {
            continue
        }
        if (i > 6) {
            break
        }
        sum = sum + i
    }
    return sum
}

func count_down(start[4]) -> [4] {
    new value[4] = start
    while (value > 0) {
        value--
    }
    return value
}

func guarded_value(value[4]) -> [4] {
    new result[4] = value
    try {
        throw 5
    } catch (err[4] as i32) {
        result = result + err
    }
    return result
}

func goto_value(seed[4]) -> [4] {
    new result[4] = seed
    goto finished
    result = 99
    finished: return result
}

func static_tick() -> [4] {
    static counter[4]
    counter = counter + 1
    return counter
}

func arithmetic_checks() -> [4] {
    new value[4] = PROJECT_SEED
    new quotient[4], remainder[4]
    quotient, remainder = divmod(17, 5)

    value = (value + 2) * 2
    value = value / 3
    value = value % 11
    value = value ** 3
    value = value << 1
    value = value >> 1
    value = (value & 0xff) | 0x10
    value = value ^ 0x03
    value = value? >= 0 ? value : 0 %4d+ 0

    if (quotient != 3 || remainder != 2) {
        return 10
    }
    if (~0 == 0) {
        return 11
    }
    if (!(true && !false) || false) {
        return 12
    }
    if (value != 219) {
        return 13
    }
    return 0
}

func assignment_checks() -> [4] {
    new a[4], b[4], c[4], flag[1] as bool, label[16] as cstr
    a = b = 7
    c %d= 30
    c %d+= 12
    c %d-= 2
    c %d*= 2
    c %d/= 4
    c %d%= 20
    flag %b= c == 0
    label = EXPECTED_LABEL

    new copied[16] as cstr
    copied %s= "wrong"
    copied %s= label

    if (a != 7 || b != 7) {
        return 20
    }
    if (c != 0 || !flag) {
        return 21
    }
    if (strcmp(copied, EXPECTED_LABEL) != 0) {
        return 22
    }
    return 0
}

func memory_checks() -> [4] {
    new bytes[32] as bytes
    set bytes as none
    set bytes as bytes

    new copied[4]
    new ptr = alloc(4)
    new heap = calloc(2, 8)

    bytes[0] = 'H'
    bytes[1] = 'S'
    bytes[2] = 0
    bytes[4:8] = 0x01020304
    copied = bytes[4:+4]

    [4]*ptr = copied
    copied = [4]*ptr + 1

    heap = realloc(heap, 24)
    memset(heap, 0, 24)
    new copied_ptr = memcpy(heap, &bytes, 3)
    new moved_destination as addr = heap? + 4
    new moved_ptr = memmove(moved_destination, heap, 3)

    new cmp[4] = memcmp(heap, &bytes, 2)
    new text[3] as cstr = "HS"
    new len = strlen(text)

    free(ptr)
    free(heap)

    if (copied != 0x01020305) {
        return 30
    }
    if (copied_ptr == 0 || moved_ptr == 0) {
        return 32
    }
    if (cmp != 0 || len != 2) {
        return 31
    }
    return 0
}

func string_checks() -> [4] {
    new dst[32] as cstr
    new src[16] as cstr
    new word[16] as cstr
    dst = "Hit"
    src = "Simple"
    word = "Compiler"
    new dst_ptr = strcpy(dst, dst)
    new cat_ptr = strcat(dst, src)
    new src_ptr = strncpy(src, word, 8)

    new match = strchr(dst, 'S')
    new base as addr = &dst
    new expected_match as addr = base? + 3
    new cmp[4] = strcmp(dst, EXPECTED_LABEL)
    if (dst_ptr == 0 || cat_ptr == 0 || src_ptr == 0) {
        return 42
    }
    if (cmp != 0 || match != expected_match) {
        return 40
    }
    if (strlen(src) != 8) {
        return 41
    }
    return 0
}

func stdlib_numeric_checks() -> [4] {
    new swapped[2] as bytes = byte_swap(0x1234 as bytes)
    new raw[1] as bytes = resize_bytes(swapped, 1)
    new resized[2] as bytes = resize_bytes(0x12345678 as bytes, 2)
    new absolute[8] = abs(-7)
    new lower[8] = min(3, 9)
    new upper[8] = max(3, 9)
    new as_float[4] %f= to_f32(42)
    new as_int[4] = to_i32(as_float)

    if (absolute != 7 || lower != 3 || upper != 9 || as_int != 42) {
        return 51
    }
    return 0
}

func float_checks() -> [4] {
    new x[4] %f= 1.5
    new y[4] %f= 2.25
    new z[4] %f= (x %f+ y) %f* 2.0
    new half[4] %f= z %f/ 2.0
    new diff[4] %f= half %f- x
    new neg[4] %f= 0.0 %f- 3.0
    new absolute[4] %f= f_abs(neg)
    new root[8] %f= f_sqrt(4.0)
    new rounded[4] = to_i32(z)
    new absolute_i[4] = to_i32(absolute)
    new root_i[4] = to_i32(root)
    new diff_i[4] = to_i32(diff)

    if (rounded != 7 || absolute_i != 3 || root_i != 2) {
        return 60
    }
    if (diff_i != 2) {
        return 61
    }
    return 0
}

func char_checks() -> [4] {
    new digit[4] = is_digit('7')
    new alpha[4] = is_alpha('A')
    new alnum[4] = is_alnum('z')
    new space[4] = is_space('\t')
    new upper[1] = to_upper('h')
    new lower[1] = to_lower('S')

    if (!digit || !alpha || !alnum || !space) {
        return 70
    }
    if (upper != 'H' || lower != 's') {
        return 71
    }
    return 0
}

func struct_checks() -> [4] {
    new metric[s1] ;Metric
    new pair[s2] ;Pair
    new view[s1] ;BufferView

    metric.count = 2
    metric.total = 40
    metric.tag %s= "ok"

    pair.left = metric.count
    pair.right = metric.total + 2
    pair[s1].left = sizeof(Metric)
    pair[s1].right = sizeof(Pair)
    view.len = sizeof(BufferView)
    view.ptr &= &metric

    if (pair.right != 42) {
        return 80
    }
    if (pair[s1].left != 24 || pair[s1].right != 8) {
        return 81
    }
    if (view.len != 12) {
        return 82
    }
    return 0
}

func file_checks() -> [4] {
    new file = fopen("/tmp/hitsimple_comprehensive_project.tmp", "w+")
    new null as addr = 0
    if (file as addr == null) {
        return 90
    }

    fprintf(file, "%d %s\n", 42, EXPECTED_LABEL)
    new flush_status[4] = fflush(file)
    new seek_status[4] = fseek(file, 0, 0)

    new first_ch[4] = fget(file)
    new pos = ftell(file)
    new end[1] = feof(file)
    new err[4] = ferror(file)
    new ch[1] = fput(file, '\n')
    new close_status[4] = fclose(file)

    if (first_ch != '4') {
        return 91
    }
    if (flush_status != 0 || seek_status != 0 || close_status != 0) {
        return 93
    }
    if (pos == 0 || err != 0 || ch == 0 || end != 0) {
        return 92
    }
    return 0
}

func output_checks() -> [4] {
    new answer[4] = 42
    print(answer as i32)
    print("\n")
    printf("%s %d\n", EXPECTED_LABEL, 42)
    put('!')
    put('\n')
    return 0
}

func process_checks() -> [4] {
    srand(1)
    new sample[4] = rand()
    new now[8] = time_ms()
    new ticks[8] = clock_ms()
    assert(1, 200)
    if (sample < 0 || now < 0 || ticks < 0) {
        return 100
    }
    return 0
}

func run_check(code[4]) -> [4] {
    if (code != 0) {
        global_status = code
    }
    return code
}

func main() -> [4] {
    global_runs = global_runs + 1
    global_status = 0

    new first[4]
    new second[4]
    first, second = make_pair(PROJECT_SEED)
    if (first != 40 || second != 41) {
        return 1
    }

    if (run_check(arithmetic_checks()) != 0) {
        return global_status
    }
    if (run_check(assignment_checks()) != 0) {
        return global_status
    }
    if (run_check(memory_checks()) != 0) {
        return global_status
    }
    if (run_check(string_checks()) != 0) {
        return global_status
    }
    if (run_check(stdlib_numeric_checks()) != 0) {
        return global_status
    }
    if (run_check(float_checks()) != 0) {
        return global_status
    }
    if (run_check(char_checks()) != 0) {
        return global_status
    }
    if (run_check(struct_checks()) != 0) {
        return global_status
    }
    if (run_check(file_checks()) != 0) {
        return global_status
    }
    if (run_check(process_checks()) != 0) {
        return global_status
    }

    if (fold_series(8) != 18) {
        return 110
    }
    if (count_down(5) != 0) {
        return 111
    }
    if (guarded_value(37) != 42) {
        return 112
    }
    if (goto_value(42) != 42) {
        return 113
    }
    if (static_tick() != 1 || static_tick() != 2) {
        return 114
    }

    if (output_checks() != 0) {
        return 115
    }

    printf("HitSimple comprehensive project OK: %d\n", second + 1)
    return global_status
}
