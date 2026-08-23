//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.cpp
//
// Identification: src/storage/index/b_plus_tree.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree.h"
#include "buffer/traced_buffer_pool_manager.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {

FULL_INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : bpm_(std::make_shared<TracedBufferPoolManager>(buffer_pool_manager)),
      index_name_(std::move(name)),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/**
 * @brief Helper function to decide whether current b+tree is empty
 * @return Returns true if this B+ tree has no keys and values.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto *header = guard.As<BPlusTreeHeaderPage>();
  return header->root_page_id_ == INVALID_PAGE_ID;
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/**
 * @brief Return the only value that associated with input key
 *
 * This method is used for point query
 *
 * @param key input key
 * @param[out] result vector that stores the only value that associated with input key, if the value exists
 * @return : true means key exists
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  // Declaration of context instance. Using the Context is not necessary but advised.
  Context ctx;
  ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
  auto *header = header_guard.As<BPlusTreeHeaderPage>();
  page_id_t cur_pid = header->root_page_id_;
  if (cur_pid == INVALID_PAGE_ID) {
    return false;
  }
  header_guard.Drop();

  ReadPageGuard cur_guard = bpm_->ReadPage(cur_pid);
  auto *page = cur_guard.As<BPlusTreePage>();

  while (!page->IsLeafPage()) {
    auto *internal = cur_guard.As<InternalPage>();
    int size = internal->GetSize();
    int lo = 1;
    int hi = size;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (comparator_(internal->KeyAt(mid), key) <= 0) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    page_id_t child_pid = internal->ValueAt(lo - 1);
    cur_guard = bpm_->ReadPage(child_pid);
    page = cur_guard.As<BPlusTreePage>();
  }

  auto *leaf = cur_guard.As<LeafPage>();
  int size = leaf->GetSize();
  int lo = 0;
  int hi = size;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (comparator_(leaf->KeyAt(mid), key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < size && comparator_(leaf->KeyAt(lo), key) == 0) {
    if (leaf->IsTombstone(lo)) {
      return false;
    }
    result->push_back(leaf->ValueAt(lo));
    return true;
  }

  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryOptimisticInsert(const KeyType &key, const ValueType &value) -> bool {
  std::vector<ReadPageGuard> guards;

  page_id_t leaf_pid = FindLeafPageOptimistic(key, guards);

  if (leaf_pid == INVALID_PAGE_ID) {
    return false;
  }

  auto *leaf = guards.back().As<LeafPage>();

  /*
   * 如果插入后会满
   * 不能乐观插入
   */
  if (leaf->GetSize() + 1 >= leaf->GetMaxSize()) {
    return false;
  }

  /*
   * 释放读锁
   */
  guards.clear();

  /*
   * 只写leaf
   */
  auto guard = bpm_->WritePage(leaf_pid);

  auto *write_leaf = guard.AsMut<LeafPage>();

  int size = write_leaf->GetSize();

  int lo = 0;
  int hi = size;

  while (lo < hi) {
    int mid = (lo + hi) / 2;

    if (comparator_(write_leaf->KeyAt(mid), key) < 0) {
      lo = mid + 1;

    } else {
      hi = mid;
    }
  }

  // duplicate key
  if (lo < size && comparator_(write_leaf->KeyAt(lo), key) == 0) {
    return false;
  }

  write_leaf->InsertAt(lo, key, value);

  return true;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/**
 * @brief Insert constant key & value pair into b+ tree
 *
 * if current tree is empty, start new tree, update root page id and insert
 * entry; otherwise, insert into leaf page.
 *
 * @param key the key to insert
 * @param value the value associated with key
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false; otherwise, return true.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // Declaration of context instance. Using the Context is not necessary but advised.
  if (TryOptimisticInsert(key, value)) {
    return true;
  }
  Context ctx;
  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto *header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();

  // empty tree
  if (header->root_page_id_ == INVALID_PAGE_ID) {
    page_id_t new_pid = bpm_->NewPage();
    WritePageGuard leaf_guard = bpm_->WritePage(new_pid);
    auto *leaf = leaf_guard.AsMut<LeafPage>();
    leaf->Init(leaf_max_size_);
    leaf->InsertAt(0, key, value);
    header->root_page_id_ = new_pid;
    return true;
  }

  // 从根下降到叶子，用writePage悲观加锁
  page_id_t cur_pid = header->root_page_id_;
  ctx.root_page_id_ = cur_pid;

  WritePageGuard cur_guard = bpm_->WritePage(cur_pid);
  auto *page = cur_guard.As<BPlusTreePage>();

  while (!page->IsLeafPage()) {
    auto *internal = cur_guard.As<InternalPage>();
    int size = internal->GetSize();
    int lo = 1;
    int hi = size;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (comparator_(internal->KeyAt(mid), key) <= 0) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    page_id_t child_pid = internal->ValueAt(lo - 1);
    ctx.write_set_.push_back(std::move(cur_guard));
    cur_guard = bpm_->WritePage(child_pid);
    page = cur_guard.As<BPlusTreePage>();
  }

  auto *leaf = cur_guard.AsMut<LeafPage>();
  int size = leaf->GetSize();

  int lo = 0;
  int hi = size;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (comparator_(leaf->KeyAt(mid), key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  if (lo < size && comparator_(leaf->KeyAt(lo), key) == 0) {
    return false;
  }

  leaf->InsertAt(lo, key, value);

  // 检查是否需要分裂
  if (leaf->GetSize() == leaf_max_size_) {
    // 创建新叶子节点
    page_id_t new_pid = bpm_->NewPage();
    WritePageGuard new_guard = bpm_->WritePage(new_pid);
    auto *new_leaf = new_guard.AsMut<LeafPage>();
    new_leaf->Init(leaf_max_size_);

    // 把右边搬过来
    leaf->Split(new_leaf);

    // 维护next page 链
    new_leaf->SetNextPageId(leaf->GetNextPageId());
    leaf->SetNextPageId(new_pid);

    KeyType up_key = new_leaf->KeyAt(0);
    InsertIntoParent(ctx, cur_guard, up_key, new_pid);
  }

  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPageOptimistic(const KeyType &key, std::vector<ReadPageGuard> &guards) -> page_id_t {
  auto header_guard = bpm_->ReadPage(header_page_id_);

  auto *header = header_guard.template As<BPlusTreeHeaderPage>();

  page_id_t page_id = header->root_page_id_;

  if (page_id == INVALID_PAGE_ID) {
    return INVALID_PAGE_ID;
  }

  while (true) {
    auto guard = bpm_->ReadPage(page_id);

    auto *page = guard.As<BPlusTreePage>();

    if (page->IsLeafPage()) {
      guards.push_back(std::move(guard));

      return page_id;
    }

    auto *internal = guard.As<InternalPage>();

    int size = internal->GetSize();

    int lo = 1;
    int hi = size;

    while (lo < hi) {
      int mid = (lo + hi) / 2;

      if (comparator_(internal->KeyAt(mid), key) <= 0) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }

    page_id = internal->ValueAt(lo - 1);

    guards.push_back(std::move(guard));
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoParent(Context &ctx, WritePageGuard &child_guard, const KeyType &up_key,
                                      page_id_t new_pid) {
  /*
   * 这个函数处理：
   *
   * 一个 child 节点 split 之后：
   *
   * 原来：
   *
   *             parent
   *                |
   *                |
   *              child
   *
   *
   * split 后：
   *
   *             parent
   *                |
   *          +-----+------+
   *          |            |
   *       child        new_child
   *
   *
   * 需要把 new_child 的信息插入 parent。
   *
   * 如果 parent 也满了：
   *     parent 继续 split
   *     再递归调用 InsertIntoParent()
   */
  // 情况1：当前child 是根（write_set_为空) 创建新根
  /*
   * 这个函数处理：
   *
   * 一个 child 节点 split 之后：
   *
   * 原来：
   *
   *             parent
   *                |
   *                |
   *              child
   *
   *
   * split 后：
   *
   *             parent
   *                |
   *          +-----+------+
   *          |            |
   *       child        new_child
   *
   *
   * 需要把 new_child 的信息插入 parent。
   *
   * 如果 parent 也满了：
   *     parent 继续 split
   *     再递归调用 InsertIntoParent()
   */
  if (ctx.write_set_.empty()) {
    page_id_t new_root_pid = bpm_->NewPage();
    WritePageGuard root_guard = bpm_->WritePage(new_root_pid);
    auto *new_root = root_guard.AsMut<InternalPage>();
    new_root->Init(internal_max_size_);
    /*
     * InternalPage 的 size 表示 child 数量。
     *
     * 新 root 有两个孩子：
     *
     *
     *              root
     *
     *          /          \
     *
     *       child       new_child
     *
     */
    new_root->SetSize(2);
    /*
     * InternalPage 的特殊布局：
     *
     * key[0] 无意义
     *
     * 例如：
     *
     * key:
     *
     *   [ invalid | 30 ]
     *
     *
     * value:
     *
     *   [ child1 | child2 ]
     *
     *
     * key[1] 保存分隔 key。
     */
    new_root->SetKeyAt(1, up_key);
    /*
     * 左孩子：
     *
     * split 前的旧节点
     */
    new_root->SetValueAt(0, child_guard.GetPageId());
    // right
    new_root->SetValueAt(1, new_pid);

    auto *header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
    header->root_page_id_ = new_root_pid;
    return;
  }

  WritePageGuard parent_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto *parent = parent_guard.AsMut<InternalPage>();

  int size = parent->GetSize();
  int lo = 1;
  int hi = size;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (comparator_(parent->KeyAt(mid), up_key) <= 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  if (parent->GetSize() < internal_max_size_) {
    parent->InsertAt(lo, up_key, new_pid);
    return;
  }

  parent->InsertAt(lo, up_key, new_pid);
  page_id_t new_parent_pid = bpm_->NewPage();
  WritePageGuard new_parent_guard = bpm_->WritePage(new_parent_pid);
  auto *new_parent = new_parent_guard.AsMut<InternalPage>();
  new_parent->Init(internal_max_size_);
  KeyType promote_key = parent->Split(new_parent);

  InsertIntoParent(ctx, parent_guard, promote_key, new_parent_pid);
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryOptimisticRemove(const KeyType &key) -> bool {
  std::vector<ReadPageGuard> guards;
  page_id_t leaf_pid = FindLeafPageOptimistic(key, guards);

  if (leaf_pid == INVALID_PAGE_ID) {
    return false;
  }

  auto *leaf = guards.back().As<LeafPage>();

  // Only proceed if the leaf has strictly more live entries than the minimum,
  // so deletion will not cause underflow.
  if (leaf->GetLiveSize() <= leaf->GetMinSize()) {
    return false;
  }

  guards.clear();  // release read locks

  auto guard = bpm_->WritePage(leaf_pid);
  auto *write_leaf = guard.AsMut<LeafPage>();

  if (write_leaf->GetLiveSize() <= write_leaf->GetMinSize()) {
    return false;
  }

  return write_leaf->Remove(key, comparator_);
}

/**
 * @brief Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 *
 * @param key input key
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  if (TryOptimisticRemove(key)) {
    return;
  }

  Context ctx;
  std::vector<page_id_t> pages_to_delete;

  // 删除可能修改 root，因此先写锁 header page。
  ctx.header_page_.emplace(bpm_->WritePage(header_page_id_));
  auto *header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();

  ctx.root_page_id_ = header->root_page_id_;

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    return;
  }

  /*
   * 第一阶段：从根一路写锁到叶子。
   *
   * write_set_:
   * [root, ..., parent, leaf]
   */
  page_id_t current_page_id = ctx.root_page_id_;

  while (true) {
    ctx.write_set_.push_back(bpm_->WritePage(current_page_id));

    auto *page = ctx.write_set_.back().AsMut<BPlusTreePage>();

    if (page->IsLeafPage()) {
      break;
    }

    auto *internal = ctx.write_set_.back().AsMut<InternalPage>();
    current_page_id = internal->Lookup(key, comparator_);
  }

  auto *leaf = ctx.write_set_.back().AsMut<LeafPage>();
  page_id_t leaf_page_id = ctx.write_set_.back().GetPageId();

  // 用来判断叶子的物理最小 key 是否变化
  std::optional<KeyType> old_first_key;
  if (leaf->GetSize() > 0) {
    old_first_key = leaf->KeyAt(0);
  }

  if (!leaf->Remove(key, comparator_)) {
    // key 不存在，或者已经被 tombstone
    return;
  }

  /*
   * 根本身就是叶子。
   *
   * 注意这里看的是 live size，而不只是物理 GetSize()。
   */
  if (ctx.IsRootPage(leaf_page_id)) {
    if (leaf->GetLiveSize() == 0) {
      header->root_page_id_ = INVALID_PAGE_ID;
      pages_to_delete.push_back(leaf_page_id);
    }

    // 一定要先释放 guard，再 DeletePage。
    ctx.write_set_.clear();
    ctx.header_page_.reset();

    for (page_id_t page_id : pages_to_delete) {
      bpm_->DeletePage(page_id);
    }
    return;
  }

  /*
   * 如果叶子物理最小 key 发生变化，要修改祖先分隔 key。
   *
   * tombstone 删除通常不会改变物理 KeyAt(0)；
   * 真正清理了最老 tombstone 时才可能改变。
   */
  if (old_first_key.has_value() && leaf->GetSize() > 0 && comparator_(old_first_key.value(), leaf->KeyAt(0)) != 0) {
    UpdateAncestorMinKey(ctx, leaf_page_id, leaf->KeyAt(0));
  }
  /*
   * 没下溢，不需要借或合并。
   */
  if (leaf->GetSize() >= leaf->GetMinSize()) {
    return;
  }

  /*
   * 第二阶段：从叶子开始向上修复下溢。
   */
  int level = static_cast<int>(ctx.write_set_.size()) - 1;

  while (level > 0) {
    auto *node = ctx.write_set_[level].AsMut<BPlusTreePage>();

    if (node->GetSize() >= node->GetMinSize()) {
      break;
    }

    auto *parent = ctx.write_set_[level - 1].AsMut<InternalPage>();

    page_id_t node_page_id = ctx.write_set_[level].GetPageId();
    int child_index = parent->ValueIndex(node_page_id);

    bool has_left = child_index > 0;
    bool has_right = child_index + 1 < parent->GetSize();

    if (node->IsLeafPage()) {
      auto *current = ctx.write_set_[level].AsMut<LeafPage>();

      /*
       * 1. 尝试从左兄弟借一个
       */
      if (has_left) {
        page_id_t left_page_id = parent->ValueAt(child_index - 1);
        auto left_guard = bpm_->WritePage(left_page_id);
        auto *left = left_guard.AsMut<LeafPage>();

        if (left->GetSize() > left->GetMinSize()) {
          left->MoveLastToFrontOf(current);

          // current 的最小 key 变化
          parent->SetKeyAt(child_index, current->KeyAt(0));
          break;
        }
      }

      /*
       * 2. 尝试从右兄弟借一个
       */
      if (has_right) {
        page_id_t right_page_id = parent->ValueAt(child_index + 1);
        auto right_guard = bpm_->WritePage(right_page_id);
        auto *right = right_guard.AsMut<LeafPage>();

        if (right->GetSize() > right->GetMinSize()) {
          right->MoveFirstToEndOf(current);

          // 右兄弟新的最小 key
          parent->SetKeyAt(child_index + 1, right->KeyAt(0));
          break;
        }
      }

      /*
       * 3. 借不到，只能合并
       */
      if (!has_left && !has_right) {
        // 无兄弟节点：父节点仅有此一个子节点，直接从父节点移除。
        parent->RemoveAt(child_index);
        pages_to_delete.push_back(node_page_id);
      } else if (has_left) {
        // current 合并进 left，删除 current
        page_id_t left_page_id = parent->ValueAt(child_index - 1);
        auto left_guard = bpm_->WritePage(left_page_id);
        auto *left = left_guard.AsMut<LeafPage>();

        current->MoveAllTo(left);
        left->SetNextPageId(current->GetNextPageId());

        parent->RemoveAt(child_index);
        pages_to_delete.push_back(node_page_id);
      } else {
        // right 合并进 current，删除 right
        page_id_t right_page_id = parent->ValueAt(child_index + 1);
        auto right_guard = bpm_->WritePage(right_page_id);
        auto *right = right_guard.AsMut<LeafPage>();

        right->MoveAllTo(current);
        current->SetNextPageId(right->GetNextPageId());

        parent->RemoveAt(child_index + 1);
        pages_to_delete.push_back(right_page_id);
      }
    } else {
      auto *current = ctx.write_set_[level].AsMut<InternalPage>();

      /*
       * 内部节点向左兄弟借
       */
      if (has_left) {
        page_id_t left_page_id = parent->ValueAt(child_index - 1);
        auto left_guard = bpm_->WritePage(left_page_id);
        auto *left = left_guard.AsMut<InternalPage>();

        if (left->GetSize() > left->GetMinSize()) {
          KeyType new_parent_key = left->MoveLastToFrontOf(current, parent->KeyAt(child_index));

          parent->SetKeyAt(child_index, new_parent_key);
          break;
        }
      }

      /*
       * 内部节点向右兄弟借
       */
      if (has_right) {
        page_id_t right_page_id = parent->ValueAt(child_index + 1);
        auto right_guard = bpm_->WritePage(right_page_id);
        auto *right = right_guard.AsMut<InternalPage>();

        if (right->GetSize() > right->GetMinSize()) {
          KeyType new_parent_key = right->MoveFirstToEndOf(current, parent->KeyAt(child_index + 1));

          parent->SetKeyAt(child_index + 1, new_parent_key);
          break;
        }
      }

      /*
       * 内部节点合并
       */
      if (!has_left && !has_right) {
        // 无兄弟节点：父节点仅有此一个子节点，直接从父节点移除。
        parent->RemoveAt(child_index);
        pages_to_delete.push_back(node_page_id);
      } else if (has_left) {
        page_id_t left_page_id = parent->ValueAt(child_index - 1);
        auto left_guard = bpm_->WritePage(left_page_id);
        auto *left = left_guard.AsMut<InternalPage>();

        // 仅当 current 有子节点时才合并（size=0 则只做移除）
        if (current->GetSize() > 0) {
          current->MoveAllTo(left, parent->KeyAt(child_index));
        }

        parent->RemoveAt(child_index);
        pages_to_delete.push_back(node_page_id);
      } else {
        page_id_t right_page_id = parent->ValueAt(child_index + 1);
        auto right_guard = bpm_->WritePage(right_page_id);
        auto *right = right_guard.AsMut<InternalPage>();

        // 仅当 right 有子节点时才合并（size=0 则只做移除）
        if (right->GetSize() > 0) {
          right->MoveAllTo(current, parent->KeyAt(child_index + 1));
        }

        parent->RemoveAt(child_index + 1);
        pages_to_delete.push_back(right_page_id);
      }
    }

    // 合并后，父节点可能下溢，继续向上。
    level--;
  }

  /*
   * 第三阶段：收缩根。
   */
  auto *root = ctx.write_set_.front().AsMut<BPlusTreePage>();
  page_id_t old_root_page_id = ctx.write_set_.front().GetPageId();

  if (!root->IsLeafPage()) {
    auto *root_internal = ctx.write_set_.front().AsMut<InternalPage>();

    if (root_internal->GetSize() == 1) {
      page_id_t new_root_page_id = root_internal->ValueAt(0);

      header->root_page_id_ = new_root_page_id;
      pages_to_delete.push_back(old_root_page_id);
    } else if (root_internal->GetSize() == 0) {
      // 根已无子节点，树置空
      header->root_page_id_ = INVALID_PAGE_ID;
      pages_to_delete.push_back(old_root_page_id);
    }
  }

  /*
   * DeletePage 前必须先释放所有 guard。
   */
  ctx.write_set_.clear();
  ctx.header_page_.reset();

  for (page_id_t page_id : pages_to_delete) {
    bpm_->DeletePage(page_id);
  }
  pages_to_delete.clear();

  // 迭代收缩根：若根只有一个子节点或是空叶子，继续折叠。
  while (true) {
    WritePageGuard hdr_guard = bpm_->WritePage(header_page_id_);
    auto *hdr = hdr_guard.AsMut<BPlusTreeHeaderPage>();
    page_id_t root_pid = hdr->root_page_id_;
    if (root_pid == INVALID_PAGE_ID) {
      break;
    }

    WritePageGuard root_guard = bpm_->WritePage(root_pid);
    auto *root_page = root_guard.AsMut<BPlusTreePage>();

    if (root_page->IsLeafPage()) {
      auto *root_leaf = root_guard.AsMut<LeafPage>();
      if (root_leaf->GetSize() == 0) {
        hdr->root_page_id_ = INVALID_PAGE_ID;
        root_guard.Drop();
        hdr_guard.Drop();
        bpm_->DeletePage(root_pid);
      }
      break;
    }

    auto *root_internal = root_guard.AsMut<InternalPage>();
    if (root_internal->GetSize() == 0) {
      // 根无子节点，树置空
      hdr->root_page_id_ = INVALID_PAGE_ID;
      root_guard.Drop();
      hdr_guard.Drop();
      bpm_->DeletePage(root_pid);
      break;
    }
    if (root_internal->GetSize() != 1) {
      break;
    }

    page_id_t new_root_pid = root_internal->ValueAt(0);
    hdr->root_page_id_ = new_root_pid;
    root_guard.Drop();
    hdr_guard.Drop();
    bpm_->DeletePage(root_pid);
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateAncestorMinKey(Context &ctx, page_id_t child_page_id, const KeyType &key) {
  while (!ctx.write_set_.empty()) {
    auto parent_guard = std::move(ctx.write_set_.back());

    ctx.write_set_.pop_back();

    auto *parent = parent_guard.AsMut<InternalPage>();

    int index = parent->ValueIndex(child_page_id);

    /*
     * child 不是最左孩子
     *
     * 它对应 parent 中一个 separator key
     *
     * 直接更新
     */
    if (index > 0) {
      parent->SetKeyAt(index, key);

      return;
    }

    /*
     * child 是最左孩子
     *
     * parent 没有 key 可以描述它
     *
     * 继续向上找
     */
    child_page_id = parent_guard.GetPageId();
  }
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/**
 * @brief Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 *
 * You may want to implement this while implementing Task #3.
 *
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  auto header_guard = bpm_->ReadPage(header_page_id_);

  auto *header = header_guard.template As<BPlusTreeHeaderPage>();

  page_id_t page_id = header->root_page_id_;

  // empty tree
  if (page_id == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, bpm_);
  }

  auto guard = bpm_->ReadPage(page_id);

  auto *page = guard.template As<BPlusTreePage>();

  // 找最左leaf
  while (!page->IsLeafPage()) {
    auto *internal = guard.template As<InternalPage>();

    page_id = internal->ValueAt(0);

    guard = bpm_->ReadPage(page_id);

    page = guard.template As<BPlusTreePage>();
  }

  auto *leaf = guard.template As<LeafPage>();

  int index = 0;

  while (index < leaf->GetSize() && leaf->IsTombstone(index)) {
    index++;
  }

  // 当前leaf全部删除
  while (index >= leaf->GetSize()) {
    page_id = leaf->GetNextPageId();

    if (page_id == INVALID_PAGE_ID) {
      return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, bpm_);
    }

    guard = bpm_->ReadPage(page_id);

    leaf = guard.template As<LeafPage>();

    index = 0;

    while (index < leaf->GetSize() && leaf->IsTombstone(index)) {
      index++;
    }
  }

  return INDEXITERATOR_TYPE(page_id, index, bpm_);
}
/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  auto header_guard = bpm_->ReadPage(header_page_id_);

  auto *header = header_guard.template As<BPlusTreeHeaderPage>();

  page_id_t page_id = header->root_page_id_;

  if (page_id == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, bpm_);
  }

  auto guard = bpm_->ReadPage(page_id);

  auto *page = guard.template As<BPlusTreePage>();

  // 找目标leaf
  while (!page->IsLeafPage()) {
    auto *internal = guard.template As<InternalPage>();

    int size = internal->GetSize();

    int lo = 1;
    int hi = size;

    while (lo < hi) {
      int mid = (lo + hi) / 2;

      if (comparator_(internal->KeyAt(mid), key) <= 0) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }

    page_id = internal->ValueAt(lo - 1);

    guard = bpm_->ReadPage(page_id);

    page = guard.template As<BPlusTreePage>();
  }

  auto *leaf = guard.template As<LeafPage>();

  int index = 0;

  // leaf 内二分
  int size = leaf->GetSize();

  int lo = 0;
  int hi = size;

  while (lo < hi) {
    int mid = (lo + hi) / 2;

    if (comparator_(leaf->KeyAt(mid), key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  index = lo;

  // 跳过 tombstone
  while (true) {
    while (index < leaf->GetSize() && leaf->IsTombstone(index)) {
      index++;
    }

    if (index < leaf->GetSize()) {
      return INDEXITERATOR_TYPE(page_id, index, bpm_);
    }

    page_id = leaf->GetNextPageId();

    if (page_id == INVALID_PAGE_ID) {
      return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, bpm_);
    }

    guard = bpm_->ReadPage(page_id);

    leaf = guard.template As<LeafPage>();

    index = 0;
  }
}
/**
 * @brief Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, bpm_); }
/**
 * @return Page id of the root of this tree
 *
 * You may want to implement this while implementing Task #3.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto *header = guard.As<BPlusTreeHeaderPage>();
  return header->root_page_id_;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
