//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.cpp
//
// Identification: src/storage/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new leaf page
 *
 * After creating a new leaf page from buffer pool, must call initialize method to set default values,
 * including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size.
 *
 * @param max_size Max size of the leaf node
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  next_page_id_ = INVALID_PAGE_ID;
  num_tombstones_ = 0;
}

/**
 * @brief Helper function for fetching tombstones of a page.
 * @return The last `NumTombs` keys with pending deletes in this page in order of recency (oldest at front).
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetTombstones() const -> std::vector<KeyType> {
  std::vector<KeyType> result;
  result.reserve(num_tombstones_);
  for (size_t i = 0; i < num_tombstones_; i++) {
    size_t idx = tombstones_[i];
    result.push_back(key_array_[idx]);
  }
  return result;
}

/**
 * Helper methods to set/get next page id
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index" (a.k.a
 * array offset)
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> const KeyType & { return key_array_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::InsertAt(int index, const KeyType &key, const ValueType &value) {
  int size = GetSize();
  for (int i = size; i > index; i--) {
    key_array_[i] = key_array_[i - 1];
    rid_array_[i] = rid_array_[i - 1];
  }
  key_array_[index] = key;
  rid_array_[index] = value;
  for (size_t i = 0; i < num_tombstones_; i++) {
    if (tombstones_[i] >= static_cast<size_t>(index)) {
      tombstones_[i]++;
    }
  }
  ChangeSizeBy(1);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Split(BPlusTreeLeafPage *new_leaf) {
  int size = GetSize();
  int split_point = size / 2;
  int right_size = size - split_point;
  for (int i = 0; i < right_size; i++) {
    new_leaf->key_array_[i] = key_array_[split_point + i];
    new_leaf->rid_array_[i] = rid_array_[split_point + i];
  }

  size_t new_num = 0;
  size_t old_num = 0;
  for (size_t i = 0; i < num_tombstones_; i++) {
    if (tombstones_[i] >= static_cast<size_t>(split_point)) {
      new_leaf->tombstones_[new_num++] = tombstones_[i] - split_point;
    } else {
      tombstones_[old_num++] = tombstones_[i];
    }
  }
  num_tombstones_ = old_num;
  new_leaf->num_tombstones_ = new_num;

  SetSize(split_point);
  new_leaf->SetSize(right_size);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAt(int index) {
  int size = GetSize();

  for (int i = index; i + 1 < size; i++) {
    key_array_[i] = key_array_[i + 1];
    rid_array_[i] = rid_array_[i + 1];
  }

  size_t write = 0;

  for (size_t i = 0; i < num_tombstones_; i++) {
    // 正在物理删除的那个 tombstone 不再保存
    if (tombstones_[i] == static_cast<size_t>(index)) {
      continue;
    }

    size_t adjusted = tombstones_[i];
    if (adjusted > static_cast<size_t>(index)) {
      adjusted--;
    }

    tombstones_[write++] = adjusted;
  }

  num_tombstones_ = write;
  ChangeSizeBy(-1);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::IsTombstone(int index) const -> bool {
  for (size_t i = 0; i < num_tombstones_; i++) {
    if (tombstones_[i] == static_cast<size_t>(index)) {
      return true;
    }
  }

  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> const ValueType & { return rid_array_[index]; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Remove(const KeyType &key, const KeyComparator &comparator) -> bool {
  for (int i = 0; i < GetSize(); i++) {
    // 找到目标 key
    if (comparator(key_array_[i], key) == 0) {
      // 已经删除
      if (IsTombstone(i)) {
        return false;
      }

      /*
       * tombstone buffer 已满（或无 tombstone 支持）
       *
       * 回退到物理删除
       */
      if (num_tombstones_ >= LEAF_PAGE_TOMB_CNT) {
        RemoveAt(i);
        return true;
      }

      /*
       * 记录删除位置
       *
       * 注意：
       *
       * 不修改 key_array_
       * 不移动元素
       *
       */
      tombstones_[num_tombstones_++] = i;

      return true;
    }
  }

  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetLiveSize() const -> int { return GetSize() - num_tombstones_; }

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
  int recipient_size = recipient->GetSize();

  // recipient 全部右移
  for (int i = recipient_size; i > 0; i--) {
    recipient->key_array_[i] = recipient->key_array_[i - 1];

    recipient->rid_array_[i] = recipient->rid_array_[i - 1];
  }

  // 当前最后一个放到 recipient 第一个
  recipient->key_array_[0] = key_array_[GetSize() - 1];

  recipient->rid_array_[0] = rid_array_[GetSize() - 1];

  recipient->ChangeSizeBy(1);
  ChangeSizeBy(-1);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
  int size = recipient->GetSize();

  recipient->key_array_[size] = key_array_[0];

  recipient->rid_array_[size] = rid_array_[0];

  recipient->ChangeSizeBy(1);

  // 当前节点左移
  for (int i = 0; i < GetSize() - 1; i++) {
    key_array_[i] = key_array_[i + 1];

    rid_array_[i] = rid_array_[i + 1];
  }

  ChangeSizeBy(-1);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient) {
  int recipient_size = recipient->GetSize();

  for (int i = 0; i < GetSize(); i++) {
    recipient->key_array_[recipient_size + i] = key_array_[i];

    recipient->rid_array_[recipient_size + i] = rid_array_[i];
  }

  recipient->ChangeSizeBy(GetSize());

  recipient->next_page_id_ = next_page_id_;

  SetSize(0);
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
