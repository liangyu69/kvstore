#pragma once
#include <cstddef>
#include <cstring>
#include <string>
#include <algorithm>


/**
 * @brief 字节切片，表示一段只读内存的视图。
 *
 * Slice 不拥有内存，只持有指向外部内存的指针和长度。
 * 调用方必须保证 Slice 存活期间，其指向的内存也存活。
 *
 * 用于表示任意二进制数据（键、值），不要求以 '\0' 结尾。
 */
class Slice{
public:
    /**
     * @brief 构造空 Slice，data 指向空字符串，长度为 0。
     */
    Slice() : data_(""), size_(0) {};

    /**
     * @brief 从指针和长度构造。
     * @param d 指向外部内存的指针，调用方保证其生命周期。
     * @param n 字节数。
     */
    Slice(const char* d, size_t n) : data_(d), size_(n) {};

    /**
     * @brief 从 std::string 构造，指向其内部缓冲区。
     * @param s 调用方保证 s 的生命周期长于本 Slice。
     */
    Slice(const std::string& s) : data_(s.data()), size_(s.size()) {}

    /**
     * @brief 返回指向数据的指针。
     */
    const char* data() const { return data_; }

    /**
     * @brief 返回字节数。
     */
    size_t size() const { return size_; }

    /**
     * @brief 是否为空（长度为 0）。
     */
    bool empty() const { return size_ == 0; }

    /**
     * @brief 访问第 i 个字节，不检查越界。
     * @param i 下标，必须 < size()。
     */

    char operator[](size_t i) const { return data_[i]; }

    /**
     * @brief 按字典序比较两个 Slice。
     * @param other 另一个 Slice。
     * @return <0 表示 this 小；0 表示相等；>0 表示 this 大。
     *
     * 先比较公共部分，公共部分相同则短的小。
     */
    int compare(const Slice& other) const {
        size_t min_len = std::min(size_, other.size_);
        int r = memcmp(data_, other.data_, min_len);
        if (r != 0) return r;
        if (size_ < other.size_) return -1;
        if (size_ > other.size_) return 1;
        return 0;
    }

    /**
     * @brief 判断两个 Slice 内容是否相同。
     * @param other 另一个 Slice。
     * @return 长度和内容都相同返回 true。
     */
    bool operator==(const Slice& other) const {
        return size_ == other.size_ && memcmp(data_, other.data_, size_) == 0;
    }

    /**
     * @brief 转换为 std::string，会拷贝一份数据。
     * @return 内容的拷贝。
     */
    std::string ToString() const { return std::string(data_, size_); }


private:
    const char* data_;
    size_t size_;
};

/**
 * @brief 比较 std::string 和 Slice，按字典序。
 * @return <0 表示 a 小；0 表示相等；>0 表示 a 大。
 */
inline int CompareStringSlice(const std::string& a, const Slice& b) {
    size_t min_len = std::min(a.size(), b.size());
    int r = memcmp(a.data(), b.data(), min_len);
    if (r != 0) return r;
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return 1;
    return 0;
}