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


Status DB::MaybeFlush() {
    const size_t kMaxMemTableSize = 100;
    
    if (memtable_->ApproximateMemoryUsage() < kMaxMemTableSize) {
        return Status::OK();
    }

    // 1. 冻结当前 MemTable
    immutable_ = std::move(memtable_);
    memtable_ = std::make_unique<Skiplist>();

    // 2. 刷盘到 SSTable
    // file_id 从 1 开始，比如生成 0001.sst
    Status s = SSTable::Write(dir_path_, next_file_id_, *immutable_);
    if (!s.ok()) {
        return s;
    }
    int dir_fd = open(dir_path_.c_str(), O_RDONLY);
    fsync(dir_fd);
    close(dir_fd);

    // 3. 加载到内存中，插入到最前面（因为是最新的）
    auto sst = SSTable::Open(dir_path_, next_file_id_);
    if (sst) {
        sstables_.insert(sstables_.begin(), std::move(sst));
    }
    
    next_file_id_++;

    // 4. 释放冻结的 MemTable
    immutable_.reset();

    std::string wal_path = dir_path_ + "/wal.log";
    
    wal_.reset(); // 关闭旧 WAL
    unlink(wal_path.c_str()); // 删掉旧文件
    wal_ = std::make_unique<WAL>(wal_path); // 创建新 WAL

    printf("[Flush] MemTable flushed to %04lu.sst\n", next_file_id_ - 1);

    // 5. 当size >=4 时触发compaction
    if (sstables_.size() >= 4) {
        std::vector<std::unique_ptr<SSTable>> inputs;
        for (auto& sst_item : sstables_) {
            inputs.push_back(std::move(sst_item));
        }
        sstables_.clear();

        auto new_id=next_file_id_++;

        Status cs = Compaction::Run(dir_path_, inputs, new_id);
        if (!cs.ok()) {
            return cs;
        }

        // 删除旧文件
        for (auto& old_sst : inputs) {
            char old_sst_path[256], old_idx_path[256];
            snprintf(old_sst_path, sizeof(old_sst_path), "%s/%04lu.sst", 
                     dir_path_.c_str(), old_sst->GetFileId());
            snprintf(old_idx_path, sizeof(old_idx_path), "%s/%04lu.idx", 
                     dir_path_.c_str(), old_sst->GetFileId());
            unlink(old_sst_path);
            unlink(old_idx_path);
            old_sst.reset();
        }

        // 加载新合并的文件
        auto merged_sst = SSTable::Open(dir_path_, new_id);
        if (merged_sst) {
            sstables_.push_back(std::move(merged_sst));
        }

        printf("[Compaction] Merged %zu files into %04lu.sst\n", 
               inputs.size(), new_id);
    }

    return Status::OK();
}


Status DB::Put(const Slice& key, const Slice& value) {
    auto s = wal_->Append(1, key, value);
    if (!s.ok()) return s;
    
    memtable_->Put(key, value);
    return MaybeFlush();
}


std::optional<std::string> DB::Get(const Slice& key) {
    // 1. 查最新的内存 MemTable
    auto r = memtable_->Get(key);
    if (r.type == Skiplist::LookupResult::kFound) {
        return r.value;
    }
    if (r.type == Skiplist::LookupResult::kDeleted) {
        return std::nullopt;  // 找到墓碑，立刻返回，不准往后查
    }

    // 2. 查冻结的 Immutable MemTable
    if (immutable_) {
        r = immutable_->Get(key);
        if (r.type == Skiplist::LookupResult::kFound) {
            return r.value;
        }
        if (r.type == Skiplist::LookupResult::kDeleted) {
            return std::nullopt;
        }
    }

    // 3. 查磁盘上的 SSTable（从新到旧）
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
    auto s = wal_->Append(0, key, Slice());
    if (!s.ok()) return s;

    memtable_->Delete(key);
    return MaybeFlush();
}