#pragma once

#include "slice.h"
#include "status.h"

#include <string>
#include <functional>
#include <mutex>

/**
 * @brief 预写日志（Write-Ahead Log）。
 *
 * 每次写操作先追加到日志并 fsync，保证崩溃后可恢复。
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
     * @brief 回放所有记录。
     * @param fn 每读到一条有效记录调用一次。
     * @return 成功返回 OK；校验失败返回 Corruption。
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
