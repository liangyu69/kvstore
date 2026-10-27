#include "db.h"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <fcntl.h>

DB::DB(const std::string& path) 
    : dir_path_(path), next_file_id_(1) {
    std::string wal_path = path + "/wal.log";
    wal_ = std::make_unique<WAL>(wal_path);
    memtable_ = std::make_unique<Skiplist>();
}

DB::~DB() {}


Status DB::WriteBatch(const std::vector<BatchOp>& ops) {
    if (ops.empty()) return Status::OK();

    std::lock_guard<std::mutex> lock(mu_);

    // 1. 批量写 WAL（循环 Append 但只 fsync 一次）
    for (const auto& op : ops) {
        wal_->AppendNoSync(op.type, Slice(op.key), Slice(op.value));
    }
    wal_->Sync();   // 统一 fsync 一次

    // 2. 批量写 MemTable
    for (const auto& op : ops) {
        if (op.type == 1) {
            memtable_->Put(Slice(op.key), Slice(op.value));
        } else {
            memtable_->Delete(Slice(op.key));
        }
    }

    return MaybeFlushLocked();
}


Status DB::Open(const std::string& path, std::unique_ptr<DB>& db) {
    if (mkdir(path.c_str(), 0755) < 0 && errno != EEXIST) {
        return Status::IOError("mkdir failed: " + std::string(strerror(errno)));
    }  
    
    db.reset(new DB(path));

    DIR* dir = opendir(path.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            // 找以 .sst 结尾的文件
            if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".sst") {
                try{
                    // 提取 file_id，比如 "0001.sst" -> 1
                    uint64_t file_id = std::stoull(filename.substr(0, filename.size() - 4));
                
                    auto sst = SSTable::Open(path, file_id);
                    if (sst) {
                        db->sstables_.push_back(std::move(sst));
                        // 更新 next_file_id_，保证下次写新文件不会覆盖
                        if (file_id >= db->next_file_id_) {
                            db->next_file_id_ = file_id + 1;
                        }
                    }
                }
                catch (...) {
                    continue;
                }
            }
            if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".tmp") {
                unlink((path + "/" + filename).c_str());
                continue;
            }
        }
        closedir(dir);
    }

    // 把 sstables_ 按编号从新到旧排序（读的时候要先读新的）
    // 因为 readdir 返回顺序可能是乱的
    std::sort(db->sstables_.begin(), db->sstables_.end(),
        [](const std::unique_ptr<SSTable>& a, const std::unique_ptr<SSTable>& b) {
            return a->GetFileId() > b->GetFileId();  // 需要给 SSTable 加个方法
        });
    
    auto s = (db)->wal_->Recover([&](char type, const Slice& key, const Slice& value) {
    if (type == 1) {
        (db)->memtable_->Put(key, value);
    } else if (type == 0) {
        (db)->memtable_->Delete(key);
    }
});
    
    return s;
}


Status DB::MaybeFlushLocked() {
    const size_t kMaxMemTableSize = 4 * 1024 * 1024;
    
    if (memtable_->ApproximateMemoryUsage() < kMaxMemTableSize) {
        return Status::OK();
    }

    // 1. 用局部变量接管当前 MemTable
    auto old = std::move(memtable_);
    memtable_ = std::make_unique<Skiplist>();
    uint64_t flush_id = next_file_id_;

    // 2. 刷盘
    Status s = SSTable::Write(dir_path_, flush_id, *old);
    if (!s.ok()) {
        // ★ 回滚：把老数据搬回去，文件编号回退
        memtable_ = std::move(old); 
        // 但因为是大锁，这期间绝对没人写，所以直接把 memtable_ 恢复即可。
        return s;
    }
    
    int dir_fd = open(dir_path_.c_str(), O_RDONLY);
    fsync(dir_fd);
    close(dir_fd);

    // 3. 加入列表
    auto sst = SSTable::Open(dir_path_, flush_id);
    if (sst) {
        sstables_.insert(sstables_.begin(), std::move(sst));
    }
    next_file_id_++;

    // 4. 释放老的 MemTable
    old.reset();

    // 5. 滚动 WAL
    std::string wal_path = dir_path_ + "/wal.log";
    wal_.reset();
    unlink(wal_path.c_str());
    wal_ = std::make_unique<WAL>(wal_path);

    printf("[Flush] MemTable flushed to %04lu.sst\n", flush_id);

    // 6. 触发 Compaction
    if (sstables_.size() >= 4) {
        std::vector<std::unique_ptr<SSTable>> inputs;
        for (auto& sst_item : sstables_) {
            inputs.push_back(std::move(sst_item));
        }
        sstables_.clear();

        uint64_t new_id = next_file_id_++;
        Status cs = Compaction::Run(dir_path_, inputs, new_id);
        
        if (!cs.ok()) {
            return cs;
        }

        for (auto& old_sst : inputs) {
            char old_sst_path[256], old_idx_path[256];
            snprintf(old_sst_path, sizeof(old_sst_path), "%s/%04lu.sst", dir_path_.c_str(), old_sst->GetFileId());
            snprintf(old_idx_path, sizeof(old_idx_path), "%s/%04lu.idx", dir_path_.c_str(), old_sst->GetFileId());
            unlink(old_sst_path);
            unlink(old_idx_path);
            old_sst.reset();
        }

        auto merged_sst = SSTable::Open(dir_path_, new_id);
        if (merged_sst) {
            sstables_.push_back(std::move(merged_sst));
        }

        printf("[Compaction] Merged %zu files into %04lu.sst\n", inputs.size(), new_id);
    }

    return Status::OK();
}


Status DB::Put(const Slice& key, const Slice& value) {
    
    std::lock_guard<std::mutex> lock(mu_);
    auto s = wal_->Append(1, key, value);
    if (!s.ok()) return s;
    memtable_->Put(key, value);
    
    return MaybeFlushLocked();
}


std::optional<std::string> DB::Get(const Slice& key) {
    std::lock_guard<std::mutex> lock(mu_);
    // 1. 查最新的内存 MemTable
    auto r = memtable_->Get(key);
    if (r.type == Skiplist::LookupResult::kFound) {
        return r.value;
    }
    if (r.type == Skiplist::LookupResult::kDeleted) {
        return std::nullopt;  // 找到墓碑，立刻返回，不准往后查
    }

    // 2. 查磁盘上的 SSTable（从新到旧）
    for (auto& sst : sstables_) {
        auto sr = sst->Get(key);
        if (sr.type == SSTable::LookupResult::kFound) {
            return sr.value;
        }
        if (sr.type == SSTable::LookupResult::kDeleted) {
            return std::nullopt;  // 在 SSTable 里找到墓碑，立刻停止
        }
    }
    
    return std::nullopt;
}


Status DB::Delete(const Slice& key) {
    
    std::lock_guard<std::mutex> lock(mu_);
    auto s = wal_->Append(0, key, Slice());
    if (!s.ok()) return s;
    memtable_->Delete(key);
    
    return MaybeFlushLocked();
}