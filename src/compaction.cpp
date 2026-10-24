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
    std::sort(all_items.begin(), all_items.end(), 
    [](const RawItem& a, const RawItem& b) {
        int cmp = a.key.compare(b.key);
        if (cmp != 0) return cmp < 0;  // key 小的在前
        return a.file_id > b.file_id;  // key 相同，文件新的在前
    }); 

    std::vector<RawItem> result;
    std::string last_key;
    bool first = true;

    for (const auto& item : all_items) {
        // 如果和上一个 key 相同，跳过（旧版本）
        if (!first && item.key == last_key) {continue;}

        last_key = item.key;
        first = false;

        if (item.type == 0) {continue;}

        result.push_back(item);
    }

    // 3. 把去重后的数据写进新的跳表
    Skiplist new_memtable;
    for (const auto& item : result) {
        if (item.type == 1) {
            new_memtable.Put(Slice(item.key), Slice(item.value));
        } else {
            //虽然type=0的都跳过了，是因为是全部合并可以丢弃墓碑；
            //未来实现部分合并时不能直接丢弃墓碑，仍需要记录。
            new_memtable.Delete(Slice(item.key));
        }
    }

    // 4. 生成新的 SSTable
    return SSTable::Write(dir_path, new_file_id, new_memtable);
}