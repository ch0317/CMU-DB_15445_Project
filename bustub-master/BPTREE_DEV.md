# CMU 15-445 BusTub B+ Tree 作业开发文档

## 测试结果

| 测试套件 | 测试数 | 结果 |
|----------|--------|------|
| `b_plus_tree_insert_test` | 4 | ✅ 全部通过 |
| `b_plus_tree_delete_test` | 3 | ✅ 全部通过 |

```
[  PASSED  ] 4 tests.   ← b_plus_tree_insert_test
[  PASSED  ] 3 tests.   ← b_plus_tree_delete_test
```

---

## 项目结构

```
src/
├── include/storage/index/
│   └── b_plus_tree.h                  # 类声明、Context、模板参数
├── include/storage/page/
│   ├── b_plus_tree_page.h             # 公共页头（size / max_size / page_type）
│   ├── b_plus_tree_internal_page.h    # 内部节点页接口
│   └── b_plus_tree_leaf_page.h        # 叶子节点页接口（含 tombstone）
├── storage/index/
│   └── b_plus_tree.cpp                # 树的核心逻辑（Insert / Remove / GetValue）
└── storage/page/
    ├── b_plus_tree_internal_page.cpp  # 内部节点页实现
    └── b_plus_tree_leaf_page.cpp      # 叶子节点页实现
```

---

## 数据结构概览

### 模板参数

```cpp
template <typename KeyType, typename ValueType,
          typename KeyComparator, ssize_t NumTombs = 0>
class BPlusTree { ... };
```

`NumTombs`：每个叶子页的 tombstone 缓冲区容量。`NumTombs=0` 表示不使用 tombstone，删除时直接物理移除；`NumTombs>0` 时优先记录 tombstone，缓冲区满则回退到物理删除。

### 页面布局

**内部节点页（InternalPage）**

```
HEADER(12B) | KEY[0](invalid) | KEY[1] | ... | KEY[n-1]
            | VAL[0]          | VAL[1] | ... | VAL[n-1]
```

- `size` = 子节点数量（即 VAL 的有效个数）
- `KEY[0]` 恒为无效，`KEY[i]`（i≥1）是 `VAL[i-1]` 与 `VAL[i]` 之间的分隔键
- `Lookup(key)` 二分找到目标子页 ID

**叶子节点页（LeafPage）**

```
HEADER(16B) | num_tombstones_ | tombstones_[0..k-1]
            | KEY[0..n-1]     | RID[0..n-1]
```

- `GetSize()` = 物理 key 数量（含 tombstone 标记的 key）
- `GetLiveSize()` = `GetSize() - num_tombstones_`（实际存活数量）
- `GetMinSize()` = `max_size / 2`（整数除法）

### Context 类

```cpp
class Context {
    std::optional<WritePageGuard> header_page_;  // 写锁住 header
    page_id_t root_page_id_;                     // 当前根 page ID 快照
    std::deque<WritePageGuard> write_set_;        // 从根到叶的写锁路径
    std::deque<ReadPageGuard> read_set_;
};
```

---

## Insert 实现

### 乐观路径（TryOptimisticInsert）

```
ReadPage(header) → ReadPage(每层内部节点) → 到达叶子
如果叶子插入后不会满（size+1 < max_size）：
    释放所有读锁 → WritePage(叶子) → InsertAt → 返回 true
否则：返回 false，退出到悲观路径
```

### 悲观路径

```
WritePage(header) → WritePage(根) → ... → WritePage(叶子)
→ InsertAt(叶子)
→ 若 size == max_size：叶子 Split
    → 创建新叶子，移入后半段，更新 next_page_id 链
    → InsertIntoParent（递归向上插入分隔键）
        → 若 write_set_ 为空（刚分裂了根）：创建新内部根
        → 否则取 write_set_.back() 作为父节点，InsertAt
            → 若父节点 size 也达到 max_size：父节点 Split，继续递归
```

**叶子 Split 切分点**：`split_point = size / 2`，左留前半，右取后半。

**内部节点 Split 切分点**：`mid = size / 2`，`KEY[mid]` 提升给父节点，`VAL[mid]` 作为新节点的最左子节点，左节点保留 `[0, mid)` 共 `mid` 个子节点。

---

## Remove 实现

### 乐观路径（TryOptimisticRemove）

```
ReadPage 遍历到叶子
若叶子 GetLiveSize() > GetMinSize()：  ← 删后不会下溢
    释放所有读锁 → WritePage(叶子) → leaf->Remove() → 返回 true
否则：返回 false，退出到悲观路径
```

### 悲观路径（分三阶段）

**阶段一：从根到叶全部加写锁**

```
write_set_ = [根, 中间内部节点..., 叶子]
leaf->Remove(key)
```

**阶段二：从叶向上修复下溢**

```
level = write_set_.size() - 1
while level > 0:
    node = write_set_[level]
    if node.GetSize() >= node.GetMinSize(): break  ← 不下溢，结束

    parent = write_set_[level-1]
    child_index = parent.ValueIndex(node_page_id)
    has_left  = child_index > 0
    has_right = child_index + 1 < parent.GetSize()

    [叶子节点]
        1. 尝试向左兄弟借：left.GetSize() > GetMinSize()
               left.MoveLastToFrontOf(current)
               parent.SetKeyAt(child_index, current.KeyAt(0))
               break
        2. 尝试向右兄弟借：right.GetSize() > GetMinSize()
               right.MoveFirstToEndOf(current)
               parent.SetKeyAt(child_index+1, right.KeyAt(0))
               break
        3. 无兄弟（no-sibling）：parent.RemoveAt(child_index)，标记删除
        4. 向左合并：current.MoveAllTo(left)，parent.RemoveAt(child_index)
        5. 向右合并：right.MoveAllTo(current)，parent.RemoveAt(child_index+1)

    [内部节点]（借用时使用父节点分隔键旋转）
        1. 向左兄弟借：left.MoveLastToFrontOf(current, parent.KeyAt(child_index))
        2. 向右兄弟借：right.MoveFirstToEndOf(current, parent.KeyAt(child_index+1))
        3. 无兄弟：parent.RemoveAt(child_index)，标记删除
        4. 向左合并（size>0 guard）：current.MoveAllTo(left, sep_key)
        5. 向右合并（size>0 guard）：right.MoveAllTo(current, sep_key)

    level--
```

**阶段三：收缩根**

```
root = write_set_.front()
若 root 是内部节点：
    size == 1：new_root = root.ValueAt(0)，删除旧根
    size == 0：root_page_id = INVALID_PAGE_ID，删除旧根

释放所有 guard → DeletePage(pages_to_delete)

迭代折叠循环（处理多层单子节点根）：
    while true:
        读 header，取 root_pid
        若 root_pid == INVALID：break
        读 root：
            若是叶子且 size==0：root_page_id=INVALID，删叶子，break
            若是叶子且 size>0：break
            若 size==0：root_page_id=INVALID，删根，break
            若 size==1：root_page_id=root.ValueAt(0)，删旧根，继续
            否则：break
```

---

## 修复的 Bug 列表

### Bug 1 — `LeafPage::Remove()` tombstone 满时静默失败

**位置**：`src/storage/page/b_plus_tree_leaf_page.cpp`

**问题**：`LEAF_PAGE_TOMB_CNT=0` 时条件 `num_tombstones_ >= 0` 恒真，所有删除返回 false。

```cpp
// 修复前
if (num_tombstones_ >= LEAF_PAGE_TOMB_CNT) { return false; }

// 修复后
if (num_tombstones_ >= LEAF_PAGE_TOMB_CNT) { RemoveAt(i); return true; }
```

---

### Bug 2 — 乐观删除路径缺失

**位置**：`src/include/storage/index/b_plus_tree.h` + `b_plus_tree.cpp`

**问题**：`Remove()` 始终走全路径写锁，导致 `OptimisticDeleteTest` 的 read_count 为 0、write_count 远超 1。

**修复**：新增 `TryOptimisticRemove()`，`Remove()` 优先调用它。

---

### Bug 3 — `InternalPage::Split` 使用 `SetSize(mid+1)` 而非 `SetSize(mid)`

**位置**：`src/storage/page/b_plus_tree_internal_page.cpp`

**问题**：`size=4, mid=2` 时，分裂后左节点应保留 `mid=2` 个子节点（P0, P1），但 `SetSize(3)` 使其保留了 P2 的过期引用。

```
分裂前：[P0 | K1 | P1 | K2↑ | P2→new | K3 | P3→new]
正确：  左=[P0,P1]  size=2     新=[P2,P3]  size=2
错误：  左=[P0,P1,P2_stale] size=3
```

过期引用在正常搜索时因父节点路由而不被访问，但在删除合并阶段会被复制到兄弟节点，最终导致已删除页面的 ID 出现在树中。

```cpp
// 修复前
SetSize(mid + 1);
// 修复后
SetSize(mid);
```

---

### Bug 4 — 无兄弟节点（no-sibling）时访问越界

**位置**：`src/storage/index/b_plus_tree.cpp`，阶段二合并部分

**问题**：原代码 `!has_left && !has_right` 分支缺失，直接进入 `else`（`has_right` 分支），调用 `parent->ValueAt(child_index + 1)`，在 `parent.size=1` 时越界（UB）。

**修复**：补充 `!has_left && !has_right` 分支，执行 `parent->RemoveAt(child_index)` 后继续向上传播下溢（`level--`）。

---

### Bug 5 — `MoveAllTo` 在 source.size=0 时复制过期 `page_id_array_[0]`

**位置**：`src/storage/index/b_plus_tree.cpp`，内部节点合并代码

**问题**：`RemoveAt(0)` 只递减 `size`，不清零内存。当 `size=0` 的节点被合并进兄弟时，`MoveAllTo` 仍无条件复制 `page_id_array_[0]`（已指向被删除的页面）。

**修复**：在调用 `MoveAllTo` 前增加 size>0 guard：

```cpp
if (current->GetSize() > 0) {
    current->MoveAllTo(left, parent->KeyAt(child_index));
}
parent->RemoveAt(child_index);
pages_to_delete.push_back(node_page_id);
```

---

### Bug 6 — 阶段三及迭代收缩循环未处理 `size=0` 的根

**位置**：`src/storage/index/b_plus_tree.cpp`，阶段三

**问题**：根 size 降为 0（无子节点）时，原代码只处理 `size==1`，导致 `root_page_id` 不被清空，树呈现为指向空内部节点的无效状态。

```cpp
// 阶段三补充
} else if (root_internal->GetSize() == 0) {
    header->root_page_id_ = INVALID_PAGE_ID;
    pages_to_delete.push_back(old_root_page_id);
}

// 迭代循环补充
if (root_internal->GetSize() == 0) {
    hdr->root_page_id_ = INVALID_PAGE_ID;
    root_guard.Drop(); hdr_guard.Drop();
    bpm_->DeletePage(root_pid);
    break;
}
```

---

## 关键设计决策

### 为何 `GetMinSize() = max_size / 2` 对正确性有重大影响

`BPlusTreePage::GetMinSize()` 使用整数除法：

```cpp
auto BPlusTreePage::GetMinSize() const -> int { return max_size_ / 2; }
```

对于 `internal_max_size = 3`：`GetMinSize() = 1`，即内部节点只有 1 个子节点时**不算下溢**。这导致：
- 只有叶子会因 size=0 而下溢
- 删除级联可能停在 size=1 的内部节点处，不再向上传播
- 阶段三的迭代折叠循环必须主动处理这些单子节点层级

### 乐观路径 vs 悲观路径的选择边界

- **乐观**：`GetLiveSize() > GetMinSize()` — 删后叶子仍有余量，无需向上传播
- **悲观**：否则，全路径加写锁，支持合并/借用/根收缩

### UpdateAncestorMinKey 的触发条件

仅当叶子物理最小 key（`KeyAt(0)`）发生变化时才需要更新祖先分隔键。tombstone 删除不移动物理 key，不触发此函数。

---

## 并发安全说明

- 所有页面访问通过 `ReadPageGuard` / `WritePageGuard` RAII 对象管理，析构时自动释放 `rwlatch_`
- 悲观路径：先持有 `header_page_` 写锁，再按从根到叶顺序加写锁，释放时按逆序（析构 `write_set_`）
- 乐观路径：持有读锁遍历，确认安全后一次性释放，再对目标叶子加写锁
- `ctx.write_set_.clear()` 和 `ctx.header_page_.reset()` 必须在 `bpm_->DeletePage()` 之前调用，否则锁仍被持有
