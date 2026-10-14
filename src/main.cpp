#include "../include/skiplist.h"
#include <cassert>
#include <cstdio>
#include<iostream>
#include <ctime>

int main() {
    srand(time(nullptr));

    Skiplist sl;

    // 基本读写
    sl.Put(Slice("name"), Slice("Alice"));
    assert(sl.Get(Slice("name")).value() == "Alice");

    // 覆盖
    sl.Put(Slice("name"), Slice("Bob"));
    assert(sl.Get(Slice("name")).value() == "Bob");

    // 删除
    assert(sl.Delete(Slice("name")) == true);
    assert(!sl.Get(Slice("name")).has_value());

    // 有序遍历
    sl.Put(Slice("c"), Slice("3"));
    sl.Put(Slice("a"), Slice("1"));
    sl.Put(Slice("b"), Slice("2"));

    printf("Iterate:\n");
    sl.Iterate([](const Slice& k, const Slice& v) {
        printf("  %s = %s\n", k.ToString().c_str(), v.ToString().c_str());
    });

    // 大数据量
    for (int i = 0; i < 100000; i++) {
        std::string key = "key_" + std::to_string(i);
        sl.Put(Slice(key), Slice("value"));
    }
    assert(sl.Get(Slice("key_50000")).value() == "value");

    printf("All tests passed\n");
    printf("Memory usage: %zu bytes\n", sl.ApproximateMemoryUsage());

    return 0;
}