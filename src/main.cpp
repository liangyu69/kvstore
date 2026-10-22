#include "db.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <cstdlib>

int main() {
    std::string path = "/tmp/kvstore_tombstone_test";
    system("rm -rf /tmp/kvstore_tombstone_test");

    {
        std::unique_ptr<DB> db;
        DB::Open(path, db);
        db->Put(Slice("name"), Slice("Alice"));
        db->Put(Slice("age"), Slice("20"));
        db->Delete(Slice("name"));   // 触发刷盘写墓碑
    }

    {
        std::unique_ptr<DB> db;
        DB::Open(path, db);
        assert(!db->Get(Slice("name")).has_value());  // 必须死透
        assert(db->Get(Slice("age")).value() == "20"); // 必须活着
    }

    printf("Tombstone persistence test passed!\n");
    return 0;
}