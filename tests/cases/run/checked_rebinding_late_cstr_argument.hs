$include <string.hsh>
new backing[8] as cstr = "a"
func change() -> result[4] as cstr {
  backing = "abc"
  return "abc"
}
func main() -> i32 {
  new small[2] as cstr
  small &= &backing
  new value as i32 = strcmp(small, change())
  return value
}
