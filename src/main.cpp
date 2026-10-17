#include "../include/wal.h"
#include <cassert>
#include <cstdio>
#include <unistd.h>

int main() {
    const char* path = "/tmp/test_wal.log";
    unlink(path);   // 删掉旧文件

    {
        WAL wal(path);
        assert(wal.Append(Slice("name"), Slice("Alice")).ok());
        assert(wal.Append(Slice("age"), Slice("20")).ok());
    }

    {
        WAL wal(path);
        int count = 0;
        auto s = wal.Recover([&](const Slice& k, const Slice& v) {
            printf("%s = %s\n", k.ToString().c_str(), v.ToString().c_str());
            count++;
        });
        assert(s.ok());
        assert(count == 2);
    }

    printf("WAL tests passed\n");
    return 0;
}