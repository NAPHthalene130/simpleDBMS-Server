#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace storage {

// 一个较为通用的 B 树实现：
// 1. 支持插入 / 查找
// 2. 节点内部使用二分查找（lower_bound）
// 3. 适合作为表中主键 -> 行数据的索引容器
//
// MinDegree = t
// - 每个节点最多 2t - 1 个键
// - 每个非根节点最少 t - 1 个键
template <typename Key, typename Value, typename Compare = std::less<Key>>
class BTree {
public:
    struct Entry {
        Key key;
        Value value;

        Entry() = default;
        Entry(const Key& k, const Value& v) : key(k), value(v) {}
        Entry(Key&& k, Value&& v) : key(std::move(k)), value(std::move(v)) {}
    };

private:
    struct Node {
        bool leaf = true;
        std::vector<Entry> entries;
        std::vector<std::unique_ptr<Node>> children;

        explicit Node(bool isLeaf = true) : leaf(isLeaf) {}
    };

public:
    explicit BTree(std::size_t minDegree = 2, Compare comp = Compare())
        : t_(minDegree), comp_(std::move(comp)), root_(std::make_unique<Node>(true)) {
        if (t_ < 2) {
            throw std::invalid_argument("BTree minDegree must be >= 2");
        }
    }

    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;

    BTree(BTree&&) noexcept = default;
    BTree& operator=(BTree&&) noexcept = default;

    void clear() {
        root_ = std::make_unique<Node>(true);
        size_ = 0;
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    bool contains(const Key& key) const {
        return find(key) != nullptr;
    }

    Value* find(const Key& key) {
        return findInternal(root_.get(), key);
    }

    const Value* find(const Key& key) const {
        return findInternal(root_.get(), key);
    }

    // 若 key 已存在，则更新 value 并返回 false；否则插入并返回 true。
    bool insert(const Key& key, const Value& value) {
        if (root_->entries.size() == maxKeys()) {
            auto newRoot = std::make_unique<Node>(false);
            newRoot->children.push_back(std::move(root_));
            splitChild(newRoot.get(), 0);
            root_ = std::move(newRoot);
        }

        bool inserted = insertNonFull(root_.get(), key, value);
        if (inserted) {
            ++size_;
        }
        return inserted;
    }

    bool insert(Key&& key, Value&& value) {
        if (root_->entries.size() == maxKeys()) {
            auto newRoot = std::make_unique<Node>(false);
            newRoot->children.push_back(std::move(root_));
            splitChild(newRoot.get(), 0);
            root_ = std::move(newRoot);
        }

        bool inserted = insertNonFull(root_.get(), std::move(key), std::move(value));
        if (inserted) {
            ++size_;
        }
        return inserted;
    }

    template <typename Visitor>
    void inorder(Visitor&& visitor) const {
        inorderInternal(root_.get(), visitor);
    }

private:
    std::size_t t_;
    Compare comp_;
    std::unique_ptr<Node> root_;
    std::size_t size_ = 0;

    std::size_t maxKeys() const { return 2 * t_ - 1; }
    std::size_t minKeys() const { return t_ - 1; }

    bool equalKey(const Key& a, const Key& b) const {
        return !comp_(a, b) && !comp_(b, a);
    }

    template <typename K>
    std::size_t lowerBoundIndex(const Node* node, const K& key) const {
        return static_cast<std::size_t>(
            std::lower_bound(
                node->entries.begin(),
                node->entries.end(),
                key,
                [this](const Entry& entry, const K& target) {
                    return comp_(entry.key, target);
                }) - node->entries.begin());
    }

    Value* findInternal(Node* node, const Key& key) {
        std::size_t idx = lowerBoundIndex(node, key);
        if (idx < node->entries.size() && equalKey(node->entries[idx].key, key)) {
            return &node->entries[idx].value;
        }
        if (node->leaf) {
            return nullptr;
        }
        return findInternal(node->children[idx].get(), key);
    }

    const Value* findInternal(const Node* node, const Key& key) const {
        std::size_t idx = lowerBoundIndex(node, key);
        if (idx < node->entries.size() && equalKey(node->entries[idx].key, key)) {
            return &node->entries[idx].value;
        }
        if (node->leaf) {
            return nullptr;
        }
        return findInternal(node->children[idx].get(), key);
    }

    void splitChild(Node* parent, std::size_t childIndex) {
        Node* fullChild = parent->children[childIndex].get();
        auto right = std::make_unique<Node>(fullChild->leaf);

        // 中位键位置：t - 1
        Entry mid = std::move(fullChild->entries[t_ - 1]);

        // 右侧键移动到新节点
        for (std::size_t i = t_; i < fullChild->entries.size(); ++i) {
            right->entries.push_back(std::move(fullChild->entries[i]));
        }

        // 若非叶子，则孩子也要拆分
        if (!fullChild->leaf) {
            for (std::size_t i = t_; i < fullChild->children.size(); ++i) {
                right->children.push_back(std::move(fullChild->children[i]));
            }
            fullChild->children.resize(t_);
        }

        fullChild->entries.resize(t_ - 1);

        parent->entries.insert(parent->entries.begin() + static_cast<std::ptrdiff_t>(childIndex), std::move(mid));
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(childIndex + 1), std::move(right));
    }

    template <typename K, typename V>
    bool insertNonFull(Node* node, K&& key, V&& value) {
        std::size_t idx = lowerBoundIndex(node, key);

        if (node->leaf) {
            if (idx < node->entries.size() && equalKey(node->entries[idx].key, key)) {
                node->entries[idx].value = std::forward<V>(value);
                return false;
            }
            node->entries.insert(
                node->entries.begin() + static_cast<std::ptrdiff_t>(idx),
                Entry(std::forward<K>(key), std::forward<V>(value))
            );
            return true;
        }

        if (idx < node->entries.size() && equalKey(node->entries[idx].key, key)) {
            node->entries[idx].value = std::forward<V>(value);
            return false;
        }

        if (node->children[idx]->entries.size() == maxKeys()) {
            splitChild(node, idx);
            if (equalKey(node->entries[idx].key, key)) {
                node->entries[idx].value = std::forward<V>(value);
                return false;
            }
            if (comp_(node->entries[idx].key, key)) {
                ++idx;
            }
        }

        return insertNonFull(node->children[idx].get(), std::forward<K>(key), std::forward<V>(value));
    }

    template <typename Visitor>
    void inorderInternal(const Node* node, Visitor& visitor) const {
        if (node->leaf) {
            for (const auto& entry : node->entries) {
                visitor(entry.key, entry.value);
            }
            return;
        }

        for (std::size_t i = 0; i < node->entries.size(); ++i) {
            inorderInternal(node->children[i].get(), visitor);
            visitor(node->entries[i].key, node->entries[i].value);
        }
        inorderInternal(node->children.back().get(), visitor);
    }
};

} // namespace storage
