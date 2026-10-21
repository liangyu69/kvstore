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
    const size_t kMaxMemTableSize = 4 * 1024 *1024;
    
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

    return Status::OK();
}


Status DB::Put(const Slice& key, const Slice& value) {
    auto s = wal_->Append(1, key, value);
    if (!s.ok()) return s;
    
    memtable_->Put(key, value);
    return MaybeFlush();
}


std::optional<std::string> DB::Get(const Slice& key) {
    auto v = memtable_->Get(key);
    if (v) return v;

    // 2. 查冻结的内存
    if (immutable_) {
        v = immutable_->Get(key);
        if (v) return v;
    }

    // 3. 查磁盘上的 SSTable（从新到旧）
    for (auto& sst : sstables_) {
        v = sst->Get(key);
        if (v) return v;
    }
    
    return std::nullopt;
}


Status DB::Delete(const Slice& key) {
    auto s = wal_->Append(0, key, Slice());
    if (!s.ok()) return s;

    memtable_->Delete(key);
    return MaybeFlush();
}