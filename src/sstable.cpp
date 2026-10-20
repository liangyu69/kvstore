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
//读取指定字节长度的数据
static ssize_t ReadExact(int fd, char* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r <= 0) return total;
        total += r;
    }
    return total;
}

static Status WriteExact(int fd, const char* data, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, data + total, n - total);
        if (w <= 0) return Status::IOError("write failed");
        total += w;
    }
    return Status::OK();
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
    bool success = true;

    memtable.Iterate([&](const Slice& key, const Slice& value) {
        if (!success) return;

        std::string buf;
        PutFixed32(buf, key.size());
        buf.append(key.data(), key.size());
        PutFixed32(buf, value.size());
        buf.append(value.data(), value.size());

        Status s = WriteExact(sst_fd, buf.data(), buf.size());
        if (!s.ok()) {
            success = false;
            return;
        }

        std::string idx_buf;
        PutFixed32(idx_buf, key.size());
        idx_buf.append(key.data(), key.size());
        PutFixed64(idx_buf, offset);
        s = WriteExact(idx_fd, idx_buf.data(), idx_buf.size());
        if (!s.ok()) {
            success = false;
            return;
        }

        offset += buf.size();
    });

    if (!success) {
        unlink(sst_name);
        unlink(idx_name);
        close(sst_fd);
        close(idx_fd);
        return Status::IOError("write failed during iteration");
    }

    fsync(sst_fd);
    fsync(idx_fd);
    close(sst_fd);
    close(idx_fd);

    return Status::OK();
}


std::unique_ptr<SSTable> SSTable::Open(const std::string& dir_path, uint64_t file_id) {
    char sst_name[256];
    char idx_name[256];
    snprintf(sst_name, sizeof(sst_name), "%s/%04lu.sst", dir_path.c_str(), file_id);
    snprintf(idx_name, sizeof(idx_name), "%s/%04lu.idx", dir_path.c_str(), file_id);

    // 1. 打开数据文件,虽然构造的sstable只需要索引，但是查数据本身还是要打开sst文件获取fd才能直接get读取
    int sst_fd = open(sst_name, O_RDONLY);
    if (sst_fd < 0) return nullptr;

    // 2. 打开索引文件，读取全部索引
    int idx_fd = open(idx_name, O_RDONLY);
    if (idx_fd < 0) {
        close(sst_fd);
        return nullptr;
    }

    std::vector<std::pair<std::string, uint64_t>> index;

    while (true) {
        uint32_t key_len;
        ssize_t n = ReadExact(idx_fd, reinterpret_cast<char*>(&key_len), 4);
        if (n < 4) break;

        std::string key(key_len, '\0');
        n = ReadExact(idx_fd, const_cast<char*>(key.data()), key_len);
        if (n < (ssize_t)key_len) break;

        uint64_t offset;
        n = ReadExact(idx_fd, reinterpret_cast<char*>(&offset), 8);
        if (n < 8) break;

        index.push_back({key, offset});
    }

    close(idx_fd);

    // 3. 创建 SSTable 对象，这里的对象有key-index， 真正的获取数据还是需要根据索引去sst文件里找
    return std::unique_ptr<SSTable>(new SSTable(sst_fd, index));

}

SSTable::SSTable(int fd, const std::vector<std::pair<std::string, uint64_t>>& index)
    : fd_(fd), index_(index) {}

SSTable::~SSTable() {
    if (fd_ >= 0) close(fd_);
}


std::optional<std::string> SSTable::Get(const Slice& key) const {
    //二分查找第一个大于等于key的键
    auto it = std::lower_bound(index_.begin(), index_.end(), key,
        [](const std::pair<std::string, uint64_t>& a, const Slice& b) {
            return CompareStringSlice(a.first, b) < 0;
        });

    if (it == index_.end() || CompareStringSlice(it->first, key) != 0) {
        return std::nullopt;   // 没找到
    }

    // 2. 拿到了偏移量，用 pread 从 .sst 读取
    uint64_t offset = it->second;

    uint32_t key_len;
    if (pread(fd_, &key_len, 4, offset) != 4) return std::nullopt;
    offset += 4 + key_len;

    uint32_t val_len;
    if (pread(fd_, &val_len, 4, offset) != 4) return std::nullopt;
    
    offset += 4;

    std::string value(val_len, '\0');
    if (val_len > 0) {
        if (pread(fd_, &value[0], val_len, offset) != (ssize_t)val_len) return std::nullopt;
    }

    return value;
}