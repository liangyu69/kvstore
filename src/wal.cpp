#include"../include/wal.h"


#include <fcntl.h>      // open
#include <unistd.h>     // write, fsync, lseek, read, close
#include <cstring>      // memcpy, strerror
#include <cerrno>       // errno
#include <string>

static void PutFixed32(std::string& buf, uint32_t v) {
    buf.append(reinterpret_cast<const char*>(&v), sizeof(v));
}


static uint32_t crc32(const char* data, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

WAL::WAL(const std::string& path) : path_(path) {
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


Status WAL::Append(char type, const Slice& key,const Slice& value){
    std::string buf;

    buf.append(4,'\0');
    buf.push_back(type);

    PutFixed32(buf,key.size());
    buf.append(key.data(), key.size());
    PutFixed32(buf, value.size());
    buf.append(value.data(), value.size());

    uint32_t crc = crc32(buf.data() + 4, buf.size() - 4);
    memcpy(&buf[0], &crc, 4);

    size_t written = 0;
    while (written < buf.size()){
        ssize_t n = write(fd_, buf.data() + written ,buf.size() - written);
        if (n <= 0){
            return Status::IOError("write failed: "+std::string(strerror(errno)));
        }
        written += n;
    }

    if (fsync(fd_) < 0) {
        return Status::IOError("fsync failed: " + std::string(strerror(errno)));
    }

    return Status::OK();
}


Status WAL::Recover(RecoverFn fn){
    lseek(fd_, 0, SEEK_SET);
    while (true) {
        uint32_t expected_crc;
        ssize_t n = read(fd_, &expected_crc, 4);
        if (n == 0) break;   
        if (n < 4) break;    

        char type;
        n = read(fd_, &type, 1);
        if (n != 1) break;

        uint32_t key_len;
        n = read(fd_, &key_len, 4);
        if (n != 4) break;

        std::string key(key_len, '\0');
        n = read(fd_, &key[0], key_len);
        if (n != (ssize_t)key_len) break;

        uint32_t val_len;
        n = read(fd_, &val_len, 4);
        if (n != 4) break;

        std::string value(val_len, '\0');
        n = read(fd_, &value[0], val_len);
        if (n != (ssize_t)val_len) break;

        std::string check_buf;
        check_buf.push_back(type);
        PutFixed32(check_buf, key_len);
        check_buf.append(key);
        PutFixed32(check_buf, val_len);
        check_buf.append(value);

        uint32_t actual_crc = crc32(check_buf.data(), check_buf.size());
        if (actual_crc != expected_crc) {
            return Status::Corruption("checksum mismatch");
        }

        fn(type, Slice(key), Slice(value));
    }

    return Status::OK();
}


Status WAL::Sync() {
    if (fsync(fd_) < 0) {
        return Status::IOError("fsync failed: " + std::string(strerror(errno)));
    }
    return Status::OK();
}
