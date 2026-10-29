
# LSM-Tree KV 存储引擎

基于 **C++17** 实现的单机持久化 KV 存储引擎。参考 OSTEP 持久化原理，采用 **LSM-Tree** 架构，支持 WAL 崩溃恢复、SSTable 刷盘、墓碑删除、全量合并与并发控制。

---

## 1. 整体架构

### 写入路径

```text
DB::Put -> WAL (追加写 + fdatasync) -> MemTable (跳表)
```

### 刷盘路径（MemTable >= 4MB）

```text
MemTable 冻结 -> 写入 SSTable (.sst + .idx) -> 清空旧 WAL -> 触发 Compaction 合并
```

### 读取路径

```text
查 MemTable -> 查磁盘上的 SSTable (从新到旧) -> 返回结果
```

### 崩溃恢复

```text
DB::Open -> 扫描目录加载所有 .sst 索引 -> 回放 WAL (CRC32 校验) -> 重建 MemTable
```

---

## 2. 核心模块

| 模块 | 职责 | 核心设计 |
|------|------|----------|
| **WAL** | 预写日志，保证崩溃不丢数据 | 追加写、CRC32 校验、尾部损坏截断、写失败中毒自锁 (`broken_`) |
| **MemTable** | 内存跳表，加速读写 | 多层索引，O(log n) 查找 |
| **SSTable** | 磁盘不可变数据文件 | `.tmp` 影子写 + `rename` 原子提交，保证断电不写坏文件 |
| **Compaction** | 清理旧数据与墓碑 | 全量多路归并，按 `file_id` 去重，物理删除墓碑 |
| **DB** | 对外接口与模块协调 | 全局大锁保护，`WriteBatch` 批量提交优化 |

---

## 3. Benchmark（性能数据）

> **测试环境：** VirtualBox (Ubuntu)，虚拟磁盘，`-O2` 优化，无 Sanitizer。
> **测试负载：** 1000 条数据，每条 Value 100 字节。

| 操作 | 耗时 | 吞吐量 | 说明 |
|------|------|--------|------|
| 单条写 | 7569 ms | 132 ops/sec | 1000 次 `fdatasync`，等待磁盘物理落盘 |
| 批量写 | 10 ms | 100,000 ops/sec | 1000 条拼一个 buffer，只 1 次 `fdatasync` |
| 随机读 | 1 ms | 1,000,000 ops/sec | 纯内存跳表，无磁盘 I/O |

**结论：** 单条写 vs 批量写，性能相差 **750 倍**。这直接证明了 `fsync` / `fdatasync` 是 LSM-Tree 写路径的绝对瓶颈，也是工业界引入 **Group Commit** 的根本原因。

---

## 4. 关键设计取舍

### 4.1 为什么选 LSM-Tree 而不是 B+ 树？

| 对比维度 | B+ 树（如 MySQL InnoDB） | LSM-Tree（本项目） |
|----------|--------------------------|---------------------|
| 写入模式 | 原地修改（In-Place Update） | 追加写（Append-Only） |
| 读性能 | 稳定 | 需要查多层（读放大） |
| 写性能 | 写放大严重，随机写开销大 | 写吞吐量极高 |
| 空间 | 紧凑 | 需要 Compaction 清理（空间放大） |
| 适用场景 | 读多写少 | **写多读少** |

### 4.2 锁策略：全局大锁 vs 细粒度锁

在开发中尝试过 `shared_mutex` 细粒度锁，性能仅提升 **20%**（因为瓶颈在 `fsync` 而不在锁），却引入了 **3 个并发数据竞态**：

1. 刷盘期间读不到数据
2. 合并期间数据消失
3. WAL 滚动丢写

最终退回 `std::mutex` 全局大锁，保证**绝对正确性**，并极大简化了 `SSTable::Open` 失败、WAL 滚动等复杂边界状态的回滚逻辑。

### 4.3 批量写优化（WriteBatch）

`WriteBatch` 把 N 条操作拼成一个 buffer，一次 `write` + 一次 `fdatasync`。

- **单条写 1000 次：** 7569ms（132 ops/sec）
- **批量写 1000 条：** 10ms（100,000 ops/sec）

> ⚠️ **注意：** `WriteBatch` 不保证事务级原子性，崩溃时可能部分生效（Batch ≠ 事务）。

---

## 5. 正确性验证

- **崩溃一致性：** 断电时 WAL 尾部的半截记录会被 CRC32 校验拦截并截断；如果截断失败则标记 `broken_`，拒绝后续写入，防止在半截垃圾后继续追加导致雪崩。

- **删除不复活（墓碑机制）：** `Delete` 不物理删除节点，而是写入墓碑标记。查询时遇到墓碑立即停止下探，刷盘和合并时物理清理。

- **刷盘原子性：** SSTable 先写 `.tmp` 文件，`fsync` 后 `rename`，确保加载的 `.sst` 永远是完整的。

- **EINTR 重试：** 所有 `read` / `write` 调用封装了 EINTR 重试，防止系统信号打断导致误判为文件损坏。

---

## 6. 已知不足与未来工作

| 不足 | 说明 | 未来方向 |
|------|------|----------|
| Batch ≠ Group Commit | 实现了应用层批量提交，但未实现并发写者的 Leader/Follower 自动合并刷盘 | 引入 Group Commit |
| 全量合并 | 每次合并全部 SSTable，存在写放大 | 引入分层合并（Leveled Compaction） |
| 无布隆过滤器 | 查询不存在的 key 需依次读所有 SSTable | 引入布隆过滤器减少无效 I/O |
| 无块缓存 | 每次读 SSTable 都是直接 `pread` | 引入 LRU Block Cache 缓存热点数据块 |
| 无多行事务 | 仅支持单记录级原子性 | 支持多行事务的 Rollback |

---

## 7. 如何运行

```bash
# 1. 进入项目目录，创建构建目录
mkdir build && cd build

# 2. 生成构建文件并编译
cmake ..
make

# 3. 运行基准测试
./bench
```

---

## 8. 项目结构

```text
kvstore/
├── include/          # 头文件
│   ├── slice.h       # 字节切片
│   ├── status.h      # 错误状态
│   ├── skiplist.h    # 内存跳表
│   ├── wal.h         # 预写日志
│   ├── sstable.h     # 磁盘数据文件
│   ├── compaction.h  # 合并逻辑
│   └── db.h          # 对外接口
├── src/              # 源文件
├── bench/            # 压测程序
├── CMakeLists.txt    # 构建脚本
└── README.md         # 项目文档
```