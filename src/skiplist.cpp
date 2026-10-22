#include "../include/skiplist.h"


struct Skiplist::Node {
    std::string key;
    std::string value;
    char type; //1=PUT,0=DELETE
    Node** next;
    int level;
};

Skiplist::Node* Skiplist::NewNode(const std::string& key,
                                  const std::string& value,
                                  char type,
                                  int level) {
    Node* n = new Node();
    n->key = key;
    n->value = value;
    n->type = type;
    n->level = level;
    n->next = new Node*[level]();
    return n;
}

Skiplist::Skiplist() : max_level_(1), mem_usage_(0) {
    head_ = NewNode("", "", 1, kMaxLevel);
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
        mem_usage_ -= next->value.size();
        next->value = value.ToString();
        next->type = 1;
        mem_usage_ += next->value.size();
        return;
    }

    int new_level = RandomLevel();
    if (new_level > max_level_) {
        for (int i = max_level_; i < new_level; i++) {
            prev[i] = head_;
        }
        max_level_ = new_level;
    }

    Node* node = NewNode(key.ToString(), value.ToString(), 1,new_level);
    for (int i = 0; i < new_level; i++) {
        node->next[i] = prev[i]->next[i];
        prev[i]->next[i] = node;
    }

    mem_usage_ += sizeof(Node) + key.size() + value.size()
            + sizeof(Node*) * new_level;
}

Skiplist::LookupResult Skiplist::Get(const Slice& key) const {
    Node* cur = head_;

    for (int i = max_level_ - 1; i >= 0; i--) {
        while (cur->next[i] != nullptr &&
               CompareStringSlice(cur->next[i]->key, key) < 0) {
            cur = cur->next[i];
        }
    }

    Node* next = cur->next[0];
    if (next != nullptr && CompareStringSlice(next->key, key) == 0) {
        if (next->type == 0) {
            return {LookupResult::kDeleted, ""};
        }
        return {LookupResult::kFound, next->value};
    }
    return {LookupResult::kNotFound, ""};
}

void Skiplist::Delete(const Slice& key) { 
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
        // 节点存在，改成墓碑
        mem_usage_ -= next->value.size();
        next->value = "";
        next->type = 0;
        // 墓碑的 value 为空，不加回 mem_usage_
        return;
    }


    // 节点不存在，插入一个新的墓碑节点
    int new_level = RandomLevel();
    if (new_level > max_level_) {
        for (int i = max_level_; i < new_level; i++) {
            prev[i] = head_;
        }
        max_level_ = new_level;
    }

    Node* node = NewNode(key.ToString(), "", 0, new_level);
    for (int i = 0; i < new_level; i++) {
        node->next[i] = prev[i]->next[i];
        prev[i]->next[i] = node;
    }

    mem_usage_ += sizeof(Node) + key.size() + sizeof(Node*) * new_level;
}


void Skiplist::Iterate(IterFn fn) const {
    Node* cur = head_->next[0];
    while(cur != nullptr){
        fn(cur->key, cur->value, cur->type);
        cur = cur->next[0];        
    }
}
