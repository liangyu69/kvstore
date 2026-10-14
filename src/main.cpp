#include "../include/skiplist.h"
#include <cassert>
#include <cstdio>
#include<iostream>

int main() {
    Skiplist sl;
    sl.Put(Slice("name"), Slice("Alice"));
    auto v = sl.Get(Slice("name"));
    assert(v.has_value() && *v == "Alice");

    auto v2 = sl.Get(Slice("not_exist"));
    assert(!v2.has_value());

    printf("Get ok\n");
    return 0;
}