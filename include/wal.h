#pragma once

#include "slice.h"
#include "status.h"

#include <string>
#include <functional>
#include <mutex>


/**
 * @brief 批量操作的类型。
 */
enum class OpType : char {
    kDelete = 0,
    kPut = 1
};

/**
 * @brief 单条批量操作。
 */
struct BatchOp {
    OpType type;
    std::string key;
    std::string value;
};

/**
 * @brief 预写日志（Write-Ahead Log）。
 *
 * 支持单条同步写与批量写，保证崩溃后可恢复。
 * 日志只追加，不修改，不删除。
 */
class WAL{
public:
    /**
     * @brief 打开或创建 WAL 文件。
     * @param path 文件路径。
     */
    explicit WAL(const std::string& path);

    /**
     * @brief 关闭文件。
     */
    ~WAL();

    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;


    /**
     * @brief 追加一条记录并 fsync。
     * @param type  操作类型，1 表示 Put，0 表示 Delete。
     * @param key   键。
     * @param value 值。Delete 时传空 Slice 即可。
     * @return 成功返回 OK；失败返回 IOError。
     */
    Status Append(char type, const Slice& key, const Slice& value);

    /**
     * @brief 批量追加多条记录，并只 fsync 一次。
     * @param ops 操作列表（type, key, value）。
     */
    Status AppendBatch(const std::vector<BatchOp>& ops);

    /**
     * @brief 回放所有记录。
     * @param fn 每读到一条有效记录调用一次。
     * @return 成功返回 OK；校验失败遇到残缺/损坏记录时截断，返回 OK。
     */
    using RecoverFn = std::function<void(char type, const Slice&, const Slice&)>;
    Status Recover(RecoverFn fn);


    /**
     * @brief 强制 fsync。
     * @return 成功返回 OK；失败返回 IOError。
     */
    Status Sync();

private:
    int fd_;
    std::string path_;
};
