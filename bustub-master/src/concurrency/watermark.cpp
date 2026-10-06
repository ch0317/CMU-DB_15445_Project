//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// watermark.cpp
//
// Identification: src/concurrency/watermark.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/watermark.h"
#include <exception>
#include "common/exception.h"

namespace bustub {

auto Watermark::AddTxn(timestamp_t read_ts) -> void {
  if (read_ts < commit_ts_) {
    throw Exception("read ts < commit ts");
  }

  current_reads_[read_ts]++;
  read_timestamps_.insert(read_ts);
  watermark_ = *read_timestamps_.begin();
}

auto Watermark::RemoveTxn(timestamp_t read_ts) -> void {
  auto it = current_reads_.find(read_ts);
  if (it == current_reads_.end()) {
    return;
  }
  if (--it->second == 0) {
    current_reads_.erase(it);
    read_timestamps_.erase(read_ts);
  }
  watermark_ = read_timestamps_.empty() ? commit_ts_ : *read_timestamps_.begin();
}

}  // namespace bustub
