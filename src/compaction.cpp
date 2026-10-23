#include "../include/compaction.h"
#include <algorithm>

// 内部结构，用于在内存中暂存从 SSTable 读出的记录
struct RawItem {
    std::string key;
    std::string value;
    char type;
    uint64_t file_id; // 记录来自哪个文件，用来判断新旧
};


Status Compaction::Run(const std::string& dir_path,
                       std::vector<std::unique_ptr<SSTable>>& inputs,
                       uint64_t new_file_id) {
    
    // 1. 把传进来的所有文件数据读出来，塞进 all_items
    std::vector<RawItem> all_items;
    for (auto& sst : inputs) {
        uint64_t id = sst->GetFileId();
        sst->Iterate([&](const Slice& k, const Slice& v, char type) {
            all_items.push_back({
                k.ToString(), 
                v.ToString(), 
                type, 
                id
            });
        });
    }

    // 2. 排序与去重逻辑
    // ...

    // 3. 把去重后的数据写进新的跳表
    // ...

    // 4. 生成新的 SSTable
    // ...

    return Status::OK();
}