#pragma once

#include "slice.h"
#include "status.h"
#include "skiplist.h" 
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

class SSTable {
public:
    /**
     * @brief 把一个跳表的数据写入磁盘，生成 .sst 和 .idx 文件。
     * @param dir_path 目录路径（如 "/tmp/kvstore_db"）。
     * @param file_id  文件编号（如 1，生成 0001.sst）。
     * @param memtable 待写入的跳表。
     * @return 成功返回 OK。
     */
    static Status Write(const std::string& dir_path, uint64_t file_id, const Skiplist& memtable);

    /**
     * @brief 打开一个 SSTable，加载索引到内存。
     * @param dir_path 目录路径。
     * @param file_id  文件编号。
     */
    static std::unique_ptr<SSTable> Open(const std::string& dir_path, uint64_t file_id);

    ~SSTable();

    /**
     * @brief 查找 key。
     */
    std::optional<std::string> Get(const Slice& key) const;

    /**
     * @brief 获取当前sstable的id。
     */
    uint64_t GetFileId() const { return file_id_; }

private:
    SSTable(int fd, uint64_t file_id, const std::vector<std::pair<std::string, uint64_t>>& index);

    int fd_;   // .sst 文件的文件描述符
    uint64_t file_id_;
    std::vector<std::pair<std::string, uint64_t>> index_; // 内存索引：key -> 文件偏移量
};