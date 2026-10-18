#include "db.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <cstdlib>

int main() {
    std::string path = "/tmp/kvstore_db";
    system("rm -rf /tmp/kvstore_db");   // 清空旧测试数据

    printf("--- 第一次运行：写入与删除 ---\n");
    {
        std::unique_ptr<DB> db;
        auto s = DB::Open(path, &db);
        assert(s.ok());

        assert(db->Put(Slice("name"), Slice("Alice")).ok());
        assert(db->Put(Slice("age"), Slice("20")).ok());
        
        // 写入后立刻删除 name
        assert(db->Delete(Slice("name")).ok());

        printf("数据已写入磁盘并执行了删除。模拟进程退出...\n");
    }

    printf("\n--- 第二次运行：模拟重启，触发崩溃恢复 ---\n");
    {
        std::unique_ptr<DB> db;
        auto s = DB::Open(path, &db);
        assert(s.ok());

        // 验证恢复结果
        auto v_name = db->Get(Slice("name"));
        auto v_age = db->Get(Slice("age"));

        // name 必须彻底消失（被 Delete 记录覆盖）
        if (v_name.has_value()) {
            printf("ERROR: 'name' 复活了！值为 %s\n", v_name->c_str());
            return 1;
        }

        // age 必须还在
        if (!v_age.has_value() || *v_age != "20") {
            printf("ERROR: 'age' 丢失或损坏！\n");
            return 1;
        }

        printf("验证成功：'name' 已被永久删除，'age' = %s 依然存在。\n", v_age->c_str());
    }

    printf("\nDB crash recovery test passed!\n");
    return 0;
}