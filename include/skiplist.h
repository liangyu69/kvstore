#pragma once
#include "slice.h"
#include <optional>
#include <functional>
#include <cstddef>


/**
 * @brief 有序内存表，基于跳表实现。
 *
 * 跳表是一种概率数据结构，通过多级索引实现期望 O(log n) 的
 * 查找、插入、删除。相比红黑树，跳表实现简单、范围查询方便、
 * 并发友好。
 *
 * 本类不线程安全，并发控制由上层 DB 负责。
 *
 * 本类禁止拷贝，因为内部持有裸指针资源。
 */
class Skiplist {
public:
    /**
     * @brief 查找结果。
     */
    struct LookupResult {
        enum Type { kNotFound, kFound, kDeleted };
        Type type;
        std::string value;
    };

    /**
     * @brief 构造一个空跳表。
     */
    Skiplist();

    /**
     * @brief 析构，释放所有节点。
     */
    ~Skiplist();

    Skiplist(const Skiplist&) = delete;
    Skiplist& operator=(const Skiplist&) = delete;

    /**
     * @brief 插入或覆盖一个键值对。
     * @param key   键，内部会拷贝一份，调用方无需保证生命周期。
     * @param value 值，内部会拷贝一份。
     *
     * 若 key 已存在，则覆盖旧值。
     */
    void Put(const Slice& key, const Slice& value);

    /**
     * @brief 查找键，返回三态结果。
     * @param key 要查找的键。
     * @return 找到返回包含值的 optional；未找到返回 std::nullopt。
     */
    LookupResult Get(const Slice& key) const;

    /**
     * @brief 删除键（写墓碑）。
     * @param key 要删除的键。
     */
    void Delete(const Slice& key);

    /**
     * @brief 遍历回调类型。
     *
     * 接受一个键值对，返回 void。用于 Iterate。
     */
    using IterFn = std::function<void(const Slice&, const Slice&, char)>;

    /**
     * @brief 按 key 升序遍历所有键值对。
     * @param fn 每遇到一个键值对调用一次。
     *
     * 遍历顺序为 key 的字典序，用于刷盘到 SSTable。
     */
    void Iterate(IterFn fn) const;

    /**
     * @brief 返回跳表占用的内存估算值（字节）。
     *
     * 用于判断 MemTable 是否已满，是否需要刷盘。
     * 值为估算，包含节点结构、key、value 的大致开销。
     */
    size_t ApproximateMemoryUsage() const { return mem_usage_; }

private:
    struct Node;

    /**
     * @brief 创建一个新节点。
     * @param key   键。
     * @param value 值。
     * @param level 节点层数。
     * @return 新节点指针。
     */
    Node* NewNode(const std::string& key, const std::string& value, char type, int level);

    /**
     * @brief 返回随机层数，范围 [1, kMaxLevel]。
     */
    int RandomLevel();

    static constexpr int kMaxLevel = 12;   

    Node* head_;
    int max_level_;     // 当前最大层数
    size_t mem_usage_;
};