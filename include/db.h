#pragma once

#include "slice.h"
#include "status.h"
#include "wal.h"
#include "skiplist.h"
#include "sstable.h"
#include "compaction.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

/**
 * @brief 对外暴露的 KV 数据库。
 *
 * 组合 WAL（持久化）和 Skiplist（内存索引）。
 */

class DB {
public:
    /**
     * @brief 打开或创建数据库。
     * @param path 数据库目录路径。
     * @param db   输出参数，成功后指向新 DB 对象。
     * @return 成功返回 OK；失败返回 IOError/Corruption。
     */
    static Status Open(const std::string& path, std::unique_ptr<DB>& db);

    ~DB();

    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;

    /**
     * @brief 写入或覆盖键值对。
     */
    Status Put(const Slice& key, const Slice& value);

    /**
     * @brief 查找键。
     * @return 找到返回值；未找到返回 nullopt。
     */
    std::optional<std::string> Get(const Slice& key);

    /**
     * @brief 删除键。
     * @return 成功返回 OK；key 不存在也返回 OK。
     */
    Status Delete(const Slice& key);

private:
    explicit DB(const std::string& path);

    /**
     * @brief 检查 MemTable 大小，若超过阈值则刷盘。
     */
    Status MaybeFlushLocked();

    std::string dir_path_;
    std::unique_ptr<WAL> wal_;
    std::unique_ptr<Skiplist> memtable_;

    //最新的sstable
    std::vector<std::unique_ptr<SSTable>> sstables_;
    //下一个sstable的编号
    uint64_t next_file_id_;

    std::mutex mu_;
};