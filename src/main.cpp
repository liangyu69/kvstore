#include "db.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <iostream>

int main() {
    std::string path = "../data/kvstore_concurrent_test";
    system("mkdir -p ../data");
    system("rm -rf ../data/kvstore_concurrent_test");

    std::unique_ptr<DB> db;
    assert(DB::Open(path, db).ok());

    const int kThreads = 4;
    const int kOps = 500;

    std::atomic<int> write_done{0};

    // 一半写，一半读，同时跑
    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();

    // 写线程
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kOps; i++) {
                std::string k = "key_" + std::to_string(t) + "_" + std::to_string(i);
                std::string v = "val_" + std::to_string(t) + "_" + std::to_string(i);
                assert(db->Put(Slice(k), Slice(v)).ok());
            }
        });
    }

    // 读线程（同时进行，可能读到写了一半的数据，不能崩）
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kOps; i++) {
                std::string k = "key_" + std::to_string(t) + "_" + std::to_string(i);
                auto v = db->Get(Slice(k));
                // 读不到是正常的（写线程可能还没写完），但不能崩溃
            }
        });
    }

    for (auto& th : threads) th.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << ms <<std::endl;

    std::cout << "--- 并发读写结束，开始验证数据完整性 ---"<< std::endl;

    // 写完之后，验证所有数据都在
    for (int t = 0; t < kThreads; t++) {
        for (int i = 0; i < kOps; i++) {
            std::string k = "key_" + std::to_string(t) + "_" + std::to_string(i);
            std::string expected = "val_" + std::to_string(t) + "_" + std::to_string(i);
            auto v = db->Get(Slice(k));
            if (!v.has_value() || *v != expected) {
                printf("ERROR: key=%s, expected=%s\n", k.c_str(), expected.c_str());
                return 1;
            }
        }
    }

    printf("Concurrent correctness test passed!\n");
    return 0;
}