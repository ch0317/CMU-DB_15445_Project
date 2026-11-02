//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// count_min_sketch.cpp
//
// Identification: src/primer/count_min_sketch.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "primer/count_min_sketch.h"

#include <queue>
#include <stdexcept>
#include <string>

namespace bustub {

/**
 * Constructor for the count-min sketch.
 *
 * @param width The width of the sketch matrix.
 * @param depth The depth of the sketch matrix.
 * @throws std::invalid_argument if width or depth are zero.
 */
template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth) : width_(width), depth_(depth) {
  /** @TODO(student) Implement this function! */
  if (width_ == 0 || depth_ == 0) {
    throw std::invalid_argument("CountMinSketch width and depth must be greater than zero.");
  }

  sketch_matrix_.resize(depth_);

  for (auto &row : sketch_matrix_) {
    std::vector<std::atomic<uint32_t>> new_row(width_);
    row = std::move(new_row);

    for (auto &counter : row) {
      counter.store(0, std::memory_order_relaxed);
    }
  }

  /** @spring2026 PLEASE DO NOT MODIFY THE FOLLOWING */
  // Initialize seeded hash functions
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch &&other) noexcept
    : width_(other.width_), depth_(other.depth_), sketch_matrix_(std::move(other.sketch_matrix_)) {
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  if (this == &other) {
    return *this;
  }

  width_ = other.width_;
  depth_ = other.depth_;

  sketch_matrix_ = std::move(other.sketch_matrix_);

  hash_functions_.clear();

  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }

  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  for (size_t i = 0; i < depth_; i++) {
    // 用原子加法代替锁
    sketch_matrix_[i][hash_functions_[i](item) % width_].fetch_add(1, std::memory_order_relaxed);
  }
}

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  if (width_ != other.width_ || depth_ != other.depth_) {
    throw std::invalid_argument("Incompatible CountMinSketch dimensions for merge.");
  }

  for (size_t i = 0; i < depth_; i++) {
    for (size_t j = 0; j < width_; j++) {
      const uint32_t a = sketch_matrix_[i][j].load(std::memory_order_relaxed);
      const uint32_t b = other.sketch_matrix_[i][j].load(std::memory_order_relaxed);

      sketch_matrix_[i][j].store(a + b, std::memory_order_relaxed);
    }
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  uint32_t min_count = std::numeric_limits<uint32_t>::max();
  for (size_t i = 0; i < depth_; i++) {
    min_count =
        std::min(min_count, sketch_matrix_[i][hash_functions_[i](item) % width_].load(std::memory_order_relaxed));
  }

  return min_count;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  for (size_t i = 0; i < depth_; i++) {
    for (size_t j = 0; j < width_; j++) {
      sketch_matrix_[i][j].store(0, std::memory_order_relaxed);
    }
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  if (k == 0) {
    return {};
  }

  using Entry = std::pair<KeyType, uint32_t>;
  std::vector<Entry> res;

  auto compare = [](Entry a, Entry b) { return a.second > b.second; };

  std::priority_queue<Entry, std::vector<Entry>, decltype(compare)> heap(compare);

  for (const auto &candidate : candidates) {
    Entry temp(candidate, Count(candidate));
    if (heap.size() < k) {
      heap.push(temp);
    } else {
      if (!compare(heap.top(), temp)) {
        heap.pop();
        heap.push(temp);
      }
    }
  }

  res.reserve(heap.size());
  while (!heap.empty()) {
    res.push_back(heap.top());
    heap.pop();
  }

  std::reverse(res.begin(), res.end());
  return res;
}

// Explicit instantiations for all types used in tests
template class CountMinSketch<std::string>;
template class CountMinSketch<int64_t>;  // For int64_t tests
template class CountMinSketch<int>;      // This covers both int and int32_t
}  // namespace bustub
