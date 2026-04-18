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
  matrix_.resize(depth);
  if (width == 0 || depth == 0) {
    throw std::invalid_argument("Invalid width.");
  }
  for (size_t i = 0; i < depth_; i++) {
    matrix_[i].resize(width);
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
    : width_(other.width_),
      depth_(other.depth_),
      hash_functions_(std::move(other.hash_functions_)),
      matrix_(std::move(other.matrix_)) {}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  if (this != &other) {
    width_ = other.width_;
    depth_ = other.depth_;
    hash_functions_ = std::move(other.hash_functions_);
    matrix_ = std::move(other.matrix_);
  }
  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  std::lock_guard<std::mutex> lock(mtx_);
  for (size_t i = 0; i < depth_; i++) {
    matrix_[i][hash_functions_[i](item)] += 1;
  }
}

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  if (width_ != other.width_ || depth_ != other.depth_) {
    throw std::invalid_argument("Incompatible CountMinSketch dimensions for merge.");
  }
  std::lock_guard<std::mutex> lock(mtx_);
  for (size_t i = 0; i < depth_; i++) {
    for (size_t j = 0; j < width_; j++) {
      matrix_[i][j] += other.matrix_[i][j];
    }
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  uint32_t result = std::numeric_limits<uint32_t>::max();
  for (size_t i = 0; i < depth_; i++) {
    auto idx = hash_functions_[i](item);
    result = std::min(result, matrix_[i][idx]);
  }
  return result;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  std::lock_guard<std::mutex> lock(mtx_);
  for (size_t i = 0; i < depth_; i++) {
    std::fill(matrix_[i].begin(), matrix_[i].end(), 0);
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  std::vector<std::pair<KeyType, uint32_t>> result;
  result.reserve(candidates.size());

  for (const auto &item : candidates) {
    result.emplace_back(item, Count(item));
  }

  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

  if (k < result.size()) {
    result.resize(k);
  }

  return result;
}

// Explicit instantiations for all types used in tests
template class CountMinSketch<std::string>;
template class CountMinSketch<int64_t>;  // For int64_t tests
template class CountMinSketch<int>;      // This covers both int and int32_t
}  // namespace bustub
