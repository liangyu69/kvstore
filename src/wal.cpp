#include"../include/wal.h"


#include <fcntl.h>      // open
#include <unistd.h>     // write, fsync, lseek, read, close
#include <cstring>      // memcpy, strerror
#include <cerrno>       // errno
#include <string>
#include <cstdio>
#include <array>



static void PutFixed32(std::string& buf, uint32_t v) {
    buf.append(reinterpret_cast<const char*>(&v), sizeof(v));
}



static const uint32_t* CrcTable() {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table.data();
}



/**
 * @brief 计算 CRC32 校验和（直接查表，无任何 if 判断，无状态纯函数）。
 */
static uint32_t crc32(const char* data, size_t n) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        crc = CrcTable()[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}


static ssize_t ReadRetry(int fd, void* buf, size_t n) {
    ssize_t r;
    do {
        r = read(fd, buf, n);
    } while (r < 0 && errno == EINTR);
    return r;
}

static ssize_t WriteRetry(int fd, const void* buf, size_t n) {
    ssize_t r;
    do {
        r = write(fd, buf, n);
    } while (r < 0 && errno == EINTR);
    if (r == 0 && n > 0) {
        errno = EIO; 
        return -1;
    }
    return r;
}

static bool RollbackAndMarkBroken(int fd, off_t start, bool& broken) {
    if (ftruncate(fd, start) < 0) {
        fprintf(stderr, "Fatal: ftruncate failed: %s\n", strerror(errno));
        broken = true;
        return false;
    }
    return true;
}

Status WAL::WriteAll(const std::string& buf) {
    if (broken_) {
        return Status::IOError("WAL is broken, reopen required");
    }

    off_t start = lseek(fd_, 0, SEEK_END);
    if (start < 0) {
        broken_ = true;
        return Status::IOError("lseek failed");
    }

    size_t written = 0;
    while (written < buf.size()) {
        ssize_t n = WriteRetry(fd_, buf.data() + written, buf.size() - written);
        if (n <= 0) {
            if (!RollbackAndMarkBroken(fd_, start, broken_)) {
                return Status::IOError("write failed, and rollback failed");
            }
            return Status::IOError("write failed");
        }
        written += n;
    }

    return Sync();
}

WAL::WAL(const std::string& path) {
    fd_ = open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        fprintf(stderr, "WAL open failed: %s\n", strerror(errno));
        abort(); 
    }
}

WAL::~WAL() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

Status WAL::Append(OpType type, const Slice& key, const Slice& value) {
    std::string buf;

    buf.append(4,'\0');
    buf.push_back(static_cast<char>(type));

    PutFixed32(buf,key.size());
    buf.append(key.data(), key.size());
    PutFixed32(buf, value.size());
    buf.append(value.data(), value.size());

    uint32_t crc = crc32(buf.data() + 4, buf.size() - 4);
    memcpy(&buf[0], &crc, 4);

    return WriteAll(buf);
}

Status WAL::AppendBatch(const std::vector<BatchOp>& ops) {
    if (ops.empty()) return Status::OK();

    std::string buf;
    for (const auto& op : ops) {
        // 预留 checksum 占位
        size_t start = buf.size();
        buf.append(4, '\0');
        
        buf.push_back(static_cast<char>(op.type));
        PutFixed32(buf, op.key.size());
        buf.append(op.key);
        PutFixed32(buf, op.value.size());
        buf.append(op.value);

        // 计算 CRC 并回填
        uint32_t crc = crc32(buf.data() + start + 4, buf.size() - start - 4);
        memcpy(&buf[start], &crc, 4);
    }

    return WriteAll(buf);
}




Status WAL::Recover(RecoverFn fn){

    lseek(fd_, 0, SEEK_SET);
    
    while (true) {
        off_t record_start = lseek(fd_, 0, SEEK_CUR);
        if (record_start < 0) {
            return Status::IOError("lseek failed: " + std::string(strerror(errno)));
        }

        auto tail_corrupt = [&] {
            if (ftruncate(fd_, record_start) < 0) {
                fprintf(stderr, "Fatal: ftruncate failed: %s\n", strerror(errno));
                broken_ = true;
            }
        };

        uint32_t expected_crc;
        ssize_t n = ReadRetry(fd_, &expected_crc, 4);
        if (n == 0) break;   
        if (n < 0) {
            return Status::IOError("read failed: " + std::string(strerror(errno)));
        }
        if (n < 4) {
            tail_corrupt();
            break;
        }   

        char type;
        n = ReadRetry(fd_, &type, 1);
        if (n < 0) return Status::IOError("read failed: " + std::string(strerror(errno)));
        if (n != 1) { tail_corrupt(); break; }
        OpType op_type = static_cast<OpType>(type);


        uint32_t key_len;
        n = ReadRetry(fd_, &key_len, 4);
        if (n < 0) return Status::IOError("read failed: " + std::string(strerror(errno)));
        if (n != 4 || key_len > 64 * 1024 * 1024) { tail_corrupt(); break; }

        std::string key(key_len, '\0');
        n = ReadRetry(fd_, &key[0], key_len);
        if (n < 0) return Status::IOError("read failed: " + std::string(strerror(errno)));
        if (n != (ssize_t)key_len) { tail_corrupt(); break; }

        uint32_t val_len;
        n = ReadRetry(fd_, &val_len, 4);
        if (n < 0) return Status::IOError("read failed: " + std::string(strerror(errno)));
        if (n != 4 || val_len > 64 * 1024 * 1024) { tail_corrupt(); break; }

        std::string value(val_len, '\0');
        n = ReadRetry(fd_, &value[0], val_len);
        if (n < 0) return Status::IOError("read failed: " + std::string(strerror(errno)));
        if (n != (ssize_t)val_len) { tail_corrupt(); break; }

        std::string check_buf;
        check_buf.push_back(type);
        PutFixed32(check_buf, key_len);
        check_buf.append(key);
        PutFixed32(check_buf, val_len);
        check_buf.append(value);

        uint32_t actual_crc = crc32(check_buf.data(), check_buf.size());
        if (actual_crc != expected_crc) {
            tail_corrupt();
            break;
        }

        fn(op_type, Slice(key), Slice(value));
    }

    if (broken_) {
        return Status::IOError("WAL tail is corrupt and rollback failed, reopen required");
    }

    return Status::OK();
}


Status WAL::Sync() {
    if (fdatasync(fd_) < 0) {
        return Status::IOError("fdatasync failed: " + std::string(strerror(errno)));
    }
    return Status::OK();
}
