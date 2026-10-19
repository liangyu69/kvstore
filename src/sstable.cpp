#include"../include/sstable.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdio>

static void PutFixed32(std::string& buf, uint32_t v) {
    buf.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

static void PutFixed64(std::string& buf, uint64_t v) {
    buf.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

static ssize_t ReadExact(int fd, char* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r <= 0) return total;
        total += r;
    }
    return total;
}


Status SSTable::Write(const std::string& dir_path, uint64_t file_id, const Skiplist& memtable) {
    //拼文件路径
    char sst_name[256];
    char idx_name[256];
    snprintf(sst_name, sizeof(sst_name), "%s/%04lu.sst", dir_path.c_str(), file_id);
    snprintf(idx_name, sizeof(idx_name), "%s/%04lu.idx", dir_path.c_str(), file_id);

    //尝试打开文件
    int sst_fd = open(sst_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int idx_fd = open(idx_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (sst_fd < 0 || idx_fd < 0) {
        if (sst_fd >= 0) close(sst_fd);
        if (idx_fd >= 0) close(idx_fd);
        return Status::IOError("open sst/idx failed");
    }

    uint64_t offset = 0;

    memtable.Iterate([&](const Slice& key, const Slice& value) {
        std::string buf;
        PutFixed32(buf, key.size());
        buf.append(key.data(), key.size());
        PutFixed32(buf, value.size());
        buf.append(value.data(), value.size());

        write(sst_fd, buf.data(), buf.size());

        std::string idx_buf;
        PutFixed32(idx_buf, key.size());
        idx_buf.append(key.data(), key.size());
        PutFixed64(idx_buf, offset);
        write(idx_fd, idx_buf.data(), idx_buf.size());

        offset += buf.size();
    });

    fsync(sst_fd);
    fsync(idx_fd);
    close(sst_fd);
    close(idx_fd);

    return Status::OK();
}