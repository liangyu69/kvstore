#include "db.h"
#include <cstdio>
#include <sys/stat.h>

DB::DB(const std::string& path) {
    std::string wal_path = path + "/wal.log";
    wal_ = std::make_unique<WAL>(wal_path);
    memtable_ = std::make_unique<Skiplist>();
}

DB::~DB() {}

Status DB::Open(const std::string& path, std::unique_ptr<DB>* db) {
    if (mkdir(path.c_str(), 0755) < 0 && errno != EEXIST) {
        return Status::IOError("mkdir failed: " + std::string(strerror(errno)));
    }  
    
    (*db).reset(new DB(path));
    
   auto s = (*db)->wal_->Recover([&](char type, const Slice& key, const Slice& value) {
    if (type == 1) {
        (*db)->memtable_->Put(key, value);
    } else if (type == 0) {
        (*db)->memtable_->Delete(key);
    }
});
    
    return s;
}

Status DB::Put(const Slice& key, const Slice& value) {
    auto s = wal_->Append(1, key, value);
    if (!s.ok()) return s;
    
    memtable_->Put(key, value);
    return Status::OK();
}


std::optional<std::string> DB::Get(const Slice& key) {
    return memtable_->Get(key);
}


Status DB::Delete(const Slice& key) {
    auto s = wal_->Append(0, key, Slice());
    if (!s.ok()) return s;

    memtable_->Delete(key);
    return Status::OK();
}