template Profile {
    id[4] as u32
}

template User {
    tag[4] as u32
    profile[4] as Profile
}

func main() {
    new first as User
    new second as User
    set first.profile.id as i32
    first.profile.id = -1
    second.profile.id = 7
    if (first.profile.id >= 0) {
        return 1
    }
    if (second.profile.id != 7) {
        return 2
    }
    return 0
}
