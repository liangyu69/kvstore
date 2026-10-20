#include "sstable.h"
#include <cassert>
#include <cstdio>

int main() {
    // 1. 造一个跳表
    Skiplist sl;
    sl.Put(Slice("a"), Slice("1"));
    sl.Put(Slice("b"), Slice("2"));
    sl.Put(Slice("c"), Slice("3"));

    // 2. 写入磁盘
    assert(SSTable::Write("/tmp", 1, sl).ok());

    // 3. 重新打开
    auto sst = SSTable::Open("/tmp", 1);

    // 4. 测试查询
    auto v1 = sst->Get(Slice("b"));
    assert(v1.has_value() && *v1 == "2");

    auto v2 = sst->Get(Slice("not_exist"));
    assert(!v2.has_value());

    printf("SSTable basic test passed\n");
    return 0;
}