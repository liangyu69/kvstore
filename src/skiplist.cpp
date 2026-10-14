#include "../include/skiplist.h"


struct Skiplist::Node {
    std::string key;
    std::string value;
    Node** next;
    int level;
};

Skiplist::Node* Skiplist::NewNode(const std::string& key,
                                  const std::string& value,
                                  int level) {
    Node* n = new Node();
    n->key = key;
    n->value = value;
    n->level = level;
    n->next = new Node*[level]();
    return n;
}

Skiplist::Skiplist() : max_level_(1), mem_usage_(0) {
    head_ = NewNode("", "", kMaxLevel);
}


Skiplist::~Skiplist() {
    Node* cur = head_->next[0];
    while (cur != nullptr)
    {
        Node* next = cur->next[0];
        delete[] cur->next;
        delete cur;
        cur = next;
    }
    delete[] head_->next;
    delete head_;
}

int Skiplist::RandomLevel() {
    int level = 1;
    while (rand() % 2 == 0 && level < kMaxLevel) {
        level++;
    }
    return level;
}

void Skiplist::Put(const Slice& key, const Slice& value) {
    Node* prev[kMaxLevel];   
    Node* cur = head_;

    for (int i = max_level_ - 1; i >= 0; i--) {
        while (cur->next[i] != nullptr &&
            CompareStringSlice(cur->next[i]->key, key) < 0) {
            cur = cur->next[i];
        }
        prev[i] = cur;   
    }

    Node* next = cur->next[0];
    if (next != nullptr && CompareStringSlice(next->key, key) == 0) {
        next->value = value.ToString();
        return;
    }

    int new_level = RandomLevel();
    if (new_level > max_level_) {
        for (int i = max_level_; i < new_level; i++) {
            prev[i] = head_;
        }
        max_level_ = new_level;
    }

    Node* node = NewNode(key.ToString(), value.ToString(), new_level);
    for (int i = 0; i < new_level; i++) {
        node->next[i] = prev[i]->next[i];
        prev[i]->next[i] = node;
    }

    mem_usage_ += sizeof(Node) + key.size() + value.size()
            + sizeof(Node*) * new_level;
}

std::optional<std::string> Skiplist::Get(const Slice& key) const {
    Node* cur = head_;

    for (int i = max_level_ - 1; i >= 0; i--) {
        while (cur->next[i] != nullptr &&
               CompareStringSlice(cur->next[i]->key, key) < 0) {
            cur = cur->next[i];
        }
    }

    Node* next = cur->next[0];
    if (next != nullptr && CompareStringSlice(next->key, key) == 0) {
        return next->value;
    }
    return std::nullopt;
}

bool Skiplist::Delete(const Slice& key) { return false; }
void Skiplist::Iterate(IterFn fn) const {}