#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>
#include <cstdint>

namespace storage {

// 一个较为通用的 B 树实现：
// 1. 支持插入 / 查找
// 2. 节点内部使用二分查找（lower_bound）
// 3. 适合作为表中主键 -> 行数据的索引容器
//
// MinDegree = t
// - 每个节点最多 2t - 1 个键
// - 每个非根节点最少 t - 1 个键

/**
 * @class BTree
 * @brief 通用 B 树模板容器
 * @author Startale
 * @tparam Key 键类型
 * @tparam Value 值类型
 * @tparam Compare 键比较器
 */
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
        bool dirty = false;
        std::uint32_t pageId = 0;
        std::vector<Entry> entries;
        std::vector<std::unique_ptr<Node>> children;

        explicit Node(bool isLeaf = true) : leaf(isLeaf) {}
    };

    std::unordered_set<Node*> dirtyNodes_;

    void markDirty(Node* node) {
        if (!node->dirty) {
            node->dirty = true;
            dirtyNodes_.insert(node);
        }
    }

public:
    /**
     * @brief 构造 B 树
     * @author Startale
     * @param minDegree 最小度，必须大于等于 2
     * @param comp 比较器
     */
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

    /**
     * @brief 清空整棵树
     * @author Startale
     */
    void clear() {
        root_ = std::make_unique<Node>(true);
        root_->pageId = 0;
        root_->dirty = true;
        size_ = 0;
        dirtyNodes_.clear();
        dirtyNodes_.insert(root_.get());
    }

    /**
     * @brief 获取元素数量
     * @author Startale
     * @return 当前元素数量
     */
    std::size_t size() const { return size_; }

    /**
     * @brief 判断容器是否为空
     * @author Startale
     * @return 是否为空
     */
    bool empty() const { return size_ == 0; }

    /**
     * @brief 判断键是否存在
     * @author Startale
     * @param key 键值
     * @return 是否存在
     */
    bool contains(const Key& key) const {
        return find(key) != nullptr;
    }

    /**
     * @brief 查找键对应的可写值
     * @author Startale
     * @param key 键值
     * @return 值指针，未找到返回 nullptr
     */
    Value* find(const Key& key) {
        return findInternal(root_.get(), key);
    }

    /**
     * @brief 查找键对应的只读值
     * @author Startale
     * @param key 键值
     * @return 值指针，未找到返回 nullptr
     */
    const Value* find(const Key& key) const {
        return findInternal(root_.get(), key);
    }

    /**
     * @brief 插入键值对（左值版本）
     * @author Startale
     * @param key 键值
     * @param value 值
     * @return true 表示新插入，false 表示更新已有键
     */
    bool insert(const Key& key, const Value& value) {
        markDirty(root_.get());
        if (root_->entries.size() == maxKeys()) {
            auto newRoot = std::make_unique<Node>(false);
            newRoot->children.push_back(std::move(root_));
            splitChild(newRoot.get(), 0);
            root_ = std::move(newRoot);
            markDirty(root_.get());
        }

        bool inserted = insertNonFull(root_.get(), key, value);
        if (inserted) {
            ++size_;
        }
        return inserted;
    }

    /**
     * @brief 插入键值对（右值版本）
     * @author Startale
     * @param key 键值
     * @param value 值
     * @return true 表示新插入，false 表示更新已有键
     */
    bool insert(Key&& key, Value&& value) {
        markDirty(root_.get());
        if (root_->entries.size() == maxKeys()) {
            auto newRoot = std::make_unique<Node>(false);
            newRoot->children.push_back(std::move(root_));
            splitChild(newRoot.get(), 0);
            root_ = std::move(newRoot);
            markDirty(root_.get());
        }

        bool inserted = insertNonFull(root_.get(), std::move(key), std::move(value));
        if (inserted) {
            ++size_;
        }
        return inserted;
    }

    bool remove(const Key& key) {
        if (!contains(key)) return false;
        markDirty(root_.get());
        removeFromNode(root_.get(), key);
        if (root_->entries.empty() && !root_->leaf) {
            auto oldRoot = std::move(root_);
            root_ = std::move(oldRoot->children[0]);
            markDirty(root_.get());
        }
        --size_;
        return true;
    }

    /**
     * @brief 中序遍历
     * @author Startale
     * @tparam Visitor 访问器类型
     * @param visitor 访问器回调
     */
    template <typename Visitor>
    void inorder(Visitor&& visitor) const {
        inorderInternal(root_.get(), visitor);
    }

    /**
     * @brief 导出 B 树节点快照文本
     * @author Startale
     * @tparam ValueFormatter 值格式化器
     * @param formatter 将 Value 转换为字符串的回调
     * @return 节点快照行列表
     */
    template <typename ValueFormatter>
    std::vector<std::string> dumpNodeLines(ValueFormatter&& formatter) const {
        std::vector<std::string> lines;
        dumpNodeLinesInternal(root_.get(), 0, lines, formatter);
        return lines;
    }

    /**
     * @struct TidNodeRef
     * @brief .tid 页序列化所需的节点结构引用
     * @author Startale
     */
    struct TidNodeRef {
        bool isLeaf = true;
        std::vector<Key> keys;
        std::vector<std::size_t> childIndices;
    };

    /**
     * @brief 导出 B 树节点结构列表（前序遍历）
     * @author Startale
     * @return 节点结构列表，children 引用 indices 为列表下标
     */
    std::vector<TidNodeRef> dumpNodeRefs() const {
        std::vector<TidNodeRef> nodes;
        dumpNodeRefsRecursive(root_.get(), nodes);
        return nodes;
    }

    /**
     * @struct DirtyPage
     * @brief 增量同步所需的脏页数据
     * @author Startale
     */
    struct DirtyPage {
        std::uint32_t pageId = 0;
        bool isLeaf = true;
        std::vector<Key> keys;
        std::vector<std::uint32_t> childPageIds;
    };

    /**
     * @brief 收集所有脏页并分配新的 pageId
     * @author Startale
     * @param rootPageId 输出当前根页ID
     * @param nextPageId 输入/输出下一个可分配页ID
     * @return 脏页列表
     */
    std::vector<DirtyPage> collectDirtyPages(std::uint32_t& rootPageId, std::uint32_t& nextPageId) {
        for (auto* node : dirtyNodes_) {
            if (node->pageId == 0) {
                node->pageId = nextPageId++;
            }
        }
        std::unordered_set<Node*> allDirty = dirtyNodes_;
        for (auto* node : allDirty) {
            if (!node->leaf) {
                for (auto& child : node->children) {
                    if (child->pageId == 0) {
                        child->pageId = nextPageId++;
                        child->dirty = true;
                        dirtyNodes_.insert(child.get());
                    }
                }
            }
        }

        std::vector<DirtyPage> pages;
        std::unordered_map<Node*, std::uint32_t> childPageMap;
        for (auto* node : dirtyNodes_) {
            DirtyPage dp;
            dp.pageId = node->pageId;
            dp.isLeaf = node->leaf;
            for (const auto& entry : node->entries) {
                dp.keys.push_back(entry.key);
            }
            if (!node->leaf) {
                for (const auto& child : node->children) {
                    if (child->pageId == 0) {
                        child->pageId = nextPageId++;
                    }
                    dp.childPageIds.push_back(child->pageId);
                }
            }
            pages.push_back(std::move(dp));
        }

        rootPageId = root_->pageId;
        return pages;
    }

    /**
     * @brief 清除所有脏标记
     * @author Startale
     */
    void clearDirtyFlags() {
        for (auto* node : dirtyNodes_) {
            node->dirty = false;
        }
        dirtyNodes_.clear();
    }

    /**
     * @brief 从外部向量按前序遍历顺序分配页ID
     * @author Startale
     * @param ids 前序排列的页ID列表，长度须等于节点总数
     */
    void assignPageIdsFrom(const std::vector<std::uint32_t>& ids) {
        std::size_t idx = 0;
        std::function<void(Node*)> assign = [&](Node* node) {
            if (idx >= ids.size()) return;
            node->pageId = ids[idx++];
            if (!node->leaf) {
                for (auto& child : node->children) {
                    assign(child.get());
                }
            }
        };
        assign(root_.get());
    }

    /**
     * @brief 判断是否已有页ID分配
     * @author Startale
     */
    bool hasPageIds() const { return root_ != nullptr && root_->pageId != 0; }

    /**
     * @brief 分配页ID到整棵树（全量写入时使用）
     * @author Startale
     */
    void assignAllPageIds(std::uint32_t& nextPageId) {
        std::function<void(Node*)> assign = [&](Node* node) {
            node->pageId = nextPageId++;
            node->dirty = true;
            dirtyNodes_.insert(node);
            if (!node->leaf) {
                for (auto& child : node->children) {
                    assign(child.get());
                }
            }
        };
        assign(root_.get());
    }

    std::vector<std::uint32_t> getNodePageIds() const {
        std::vector<std::uint32_t> ids;
        std::function<void(const Node*)> collect = [&](const Node* node) {
            ids.push_back(node->pageId);
            if (!node->leaf) {
                for (const auto& child : node->children) {
                    collect(child.get());
                }
            }
        };
        collect(root_.get());
        return ids;
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

        markDirty(parent);
        markDirty(fullChild);
        markDirty(parent->children[childIndex + 1].get());
    }

    template <typename K, typename V>
    bool insertNonFull(Node* node, K&& key, V&& value) {
        markDirty(node);
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

    void removeFromNode(Node* node, const Key& key) {
        std::size_t idx = lowerBoundIndex(node, key);
        if (idx < node->entries.size() && equalKey(node->entries[idx].key, key)) {
            if (node->leaf) {
                node->entries.erase(node->entries.begin() + static_cast<std::ptrdiff_t>(idx));
                return;
            }
            removeFromInternal(node, idx);
            return;
        }
        if (node->leaf) return;
        bool lastChild = (idx == node->entries.size());
        if (node->children[idx]->entries.size() < t_) {
            fill(node, idx);
        }
        if (lastChild && idx > node->entries.size()) {
            removeFromNode(node->children[idx - 1].get(), key);
        } else {
            removeFromNode(node->children[idx].get(), key);
        }
    }

    void removeFromInternal(Node* node, std::size_t idx) {
        Key k = node->entries[idx].key;
        if (node->children[idx]->entries.size() >= t_) {
            Entry pred = getPredecessor(node->children[idx].get());
            node->entries[idx] = pred;
            removeFromNode(node->children[idx].get(), pred.key);
        } else if (node->children[idx + 1]->entries.size() >= t_) {
            Entry succ = getSuccessor(node->children[idx + 1].get());
            node->entries[idx] = succ;
            removeFromNode(node->children[idx + 1].get(), succ.key);
        } else {
            merge(node, idx);
            removeFromNode(node->children[idx].get(), k);
        }
    }

    Entry getPredecessor(Node* node) {
        while (!node->leaf) node = node->children.back().get();
        return node->entries.back();
    }

    Entry getSuccessor(Node* node) {
        while (!node->leaf) node = node->children.front().get();
        return node->entries.front();
    }

    void fill(Node* node, std::size_t idx) {
        if (idx > 0 && node->children[idx - 1]->entries.size() >= t_) {
            borrowFromPrev(node, idx);
        } else if (idx + 1 < node->children.size() && node->children[idx + 1]->entries.size() >= t_) {
            borrowFromNext(node, idx);
        } else if (idx < node->entries.size()) {
            merge(node, idx);
        } else {
            merge(node, idx - 1);
        }
    }

    void borrowFromPrev(Node* node, std::size_t idx) {
        Node* child = node->children[idx].get();
        Node* sibling = node->children[idx - 1].get();
        child->entries.insert(child->entries.begin(), node->entries[idx - 1]);
        node->entries[idx - 1] = sibling->entries.back();
        sibling->entries.pop_back();
        if (!child->leaf) {
            child->children.insert(child->children.begin(), std::move(sibling->children.back()));
            sibling->children.pop_back();
        }
        markDirty(node); markDirty(child); markDirty(sibling);
    }

    void borrowFromNext(Node* node, std::size_t idx) {
        Node* child = node->children[idx].get();
        Node* sibling = node->children[idx + 1].get();
        child->entries.push_back(node->entries[idx]);
        node->entries[idx] = sibling->entries.front();
        sibling->entries.erase(sibling->entries.begin());
        if (!child->leaf) {
            child->children.push_back(std::move(sibling->children.front()));
            sibling->children.erase(sibling->children.begin());
        }
        markDirty(node); markDirty(child); markDirty(sibling);
    }

    void merge(Node* node, std::size_t idx) {
        Node* child = node->children[idx].get();
        Node* sibling = node->children[idx + 1].get();
        child->entries.push_back(node->entries[idx]);
        node->entries.erase(node->entries.begin() + static_cast<std::ptrdiff_t>(idx));
        for (auto& e : sibling->entries) child->entries.push_back(std::move(e));
        if (!child->leaf) {
            for (auto& c : sibling->children) child->children.push_back(std::move(c));
        }
        node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(idx + 1));
        markDirty(node); markDirty(child);
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

    template <typename ValueFormatter>
    void dumpNodeLinesInternal(const Node* node,
                               std::size_t depth,
                               std::vector<std::string>& lines,
                               ValueFormatter& formatter) const {
        std::ostringstream oss;
        oss << "depth=" << depth
            << ";leaf=" << (node->leaf ? 1 : 0)
            << ";entries=";

        for (std::size_t i = 0; i < node->entries.size(); ++i) {
            if (i != 0) {
                oss << ",";
            }
            oss << node->entries[i].key << "->" << formatter(node->entries[i].value);
        }
        lines.push_back(oss.str());

        if (!node->leaf) {
            for (const auto& child : node->children) {
                dumpNodeLinesInternal(child.get(), depth + 1, lines, formatter);
            }
        }
    }

    void dumpNodeRefsRecursive(const Node* node, std::vector<TidNodeRef>& nodes) const {
        const std::size_t idx = nodes.size();
        nodes.emplace_back();
        nodes[idx].isLeaf = node->leaf;
        for (const auto& entry : node->entries) {
            nodes[idx].keys.push_back(entry.key);
        }
        if (!node->leaf) {
            for (const auto& child : node->children) {
                nodes[idx].childIndices.push_back(nodes.size());
                dumpNodeRefsRecursive(child.get(), nodes);
            }
        }
    }
};

} // namespace storage
