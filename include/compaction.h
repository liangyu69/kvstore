#pragma once
#include "sstable.h"
#include "status.h"
#include <string>
#include <vector>
#include <memory>

class Compaction {
public:
    /**
     * @brief 把多个 SSTable 合并成一个新的 SSTable。
     * @param dir_path 目录路径。
     * @param inputs 需要合并的 SSTable 列表（**必须按文件编号从大到小排序**）。
     * @param new_file_id 新生成的文件编号。
     * @return 成功返回 OK。
     */
    static Status Run(const std::string& dir_path, 
                      std::vector<std::unique_ptr<SSTable>>& inputs,
                      uint64_t new_file_id);
};