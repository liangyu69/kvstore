#include "db.h"
#include "sstable.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <cstdlib>

int main() {
    std::string path = "/tmp/kvstore_iter_test";
    system("rm -rf /tmp/kvstore_iter_test");

    printf("--- 写入数据 ---\n");
    {
        std::unique_ptr<DB> db;
        DB::Open(path, db);
        // 写 3 条，触发刷盘（阈值 100）
        db->Put(Slice("b"), Slice("2"));
        db->Put(Slice("a"), Slice("1"));
        db->Put(Slice("c"), Slice("3"));
        db->Delete(Slice("a"));  // 删掉 a，看看墓碑
    }

    printf("--- 遍历 0001.sst ---\n");
    auto sst = SSTable::Open(path, 1);
    if (!sst) {
        printf("0001.sst not found\n");
        return 1;
    }

    sst->Iterate([](const Slice& k, const Slice& v, char type) {
        printf("  key=%s, value=%s, type=%d\n",
               k.ToString().c_str(),
               v.ToString().c_str(),
               (int)type);
    });

    auto sst2 = SSTable::Open(path, 2);
    if (!sst2) {
        printf("0002.sst not found\n");
        return 1;
    }

    sst2->Iterate([](const Slice& k, const Slice& v, char type) {
        printf("  key=%s, value=%s, type=%d\n",
               k.ToString().c_str(),
               v.ToString().c_str(),
               (int)type);
    });

    printf("Iterate test passed!\n");
    return 0;
}