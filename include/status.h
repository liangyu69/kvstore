#pragma once

#include<string>

/**
 * @brief 操作结果状态。
 *
 * 表示一个操作是成功还是失败，失败时携带错误码和消息。
 * 存储引擎中所有可能失败的操作都返回 Status。
 */
class Status{
public:
    /**
     * @brief 构造成功状态。
     */
    Status() : code_(kOk) {}

    /**
     * @brief 返回成功状态。
     */
    static Status OK() { return Status(); }

    /**
     * @brief 构造 I/O 错误状态。
     * @param msg 错误描述。
     */
    static Status IOError(const std::string& msg) {
        return Status(kIOError, msg);
    }

    /**
     * @brief 构造数据损坏状态。
     * @param msg 错误描述。
     */
    static Status Corruption(const std::string& msg) {
        return Status(kCorruption, msg);
    }

    /**
     * @brief 构造未找到状态。
     * @param msg 错误描述。
     */
    static Status NotFound(const std::string& msg) {
        return Status(kNotFound, msg);
    }

    /**
     * @brief 是否成功。
     */
    bool ok() const { return code_ == kOk; }

    /**
     * @brief 是否 I/O 错误。
     */
    bool IsIOError() const { return code_ == kIOError; }

    /**
     * @brief 是否数据损坏。
     */
    bool IsCorruption() const { return code_ == kCorruption; }

    /**
     * @brief 是否未找到。
     */
    bool IsNotFound() const { return code_ == kNotFound; }

    /**
     * @brief 返回可读的字符串描述。
     */
    std::string ToString() const {
        if (ok()) return "OK";
        return msg_;
    }
private:
    enum Code {
        kOk = 0,
        kIOError = 1,
        kCorruption = 2,
        kNotFound = 3,
    };

    Status(Code code, const std::string& msg) : code_(code), msg_(msg) {}

    Code code_;
    std::string msg_;
};