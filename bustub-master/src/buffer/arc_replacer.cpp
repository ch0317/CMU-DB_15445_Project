// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <optional>
#include "common/config.h"

namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> { 
    std::lock_guard<std::mutex> lock(latch_);

    if(curr_size_ == 0){
        return std::nullopt;
    }

    auto try_evict_from_mru = [&]() -> std::optional<frame_id_t> {
        for(auto it = mru_.end(); it != mru_.begin();){
           --it;

           frame_id_t fid = *it;
           auto &status = alive_map_[fid];

           if(status->evictable_){
            page_id_t pid = status->page_id_;
            mru_ghost_.push_front(pid);

            auto ghost_status = std::make_shared<FrameStatus>(pid, fid, false, ArcStatus::MRU_GHOST);
            ghost_status->ghost_iter_ = mru_ghost_.begin();
            ghost_map_[pid] = ghost_status;

            mru_.erase(it);
            alive_map_.erase(fid);
            curr_size_--;

            return fid;
           }

        }

        return std::nullopt;
    };

    auto try_evict_from_mfu = [&]() -> std::optional<frame_id_t>  {
      for(auto it = mfu_.end(); it != mfu_.begin();){
        --it;
        frame_id_t fid = *it;
        auto &status = alive_map_[fid];
        if(status->evictable_){
            page_id_t pid = status->page_id_;
            mfu_ghost_.push_front(pid);
            auto ghost_status = std::make_shared<FrameStatus>(pid, fid, false, ArcStatus::MFU_GHOST);
            ghost_status->ghost_iter_ = mfu_ghost_.begin();
            ghost_map_[pid] = ghost_status;
            mfu_.erase(it);
            alive_map_.erase(fid);
            curr_size_--;
            return fid;
        }
      }    
      return std::nullopt;
    };

    if(mru_.size() >= mru_target_size_) {
        auto result = try_evict_from_mru();

        if(result) {
            return result;
        }

        return try_evict_from_mfu();
    }

    auto result = try_evict_from_mfu();

    if(result) {
        return result;
    }

    return try_evict_from_mru();

}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
    std::lock_guard<std::mutex> lock(latch_);

    // Case 1: frame is already alive in MRU or MFU -> promote to the front of MRU or MFU
    auto alive_it = alive_map_.find(frame_id);
    if (alive_it != alive_map_.end()) {
        auto &status = alive_it->second;

        if(status->arc_status_ == ArcStatus::MRU) {
            mru_.erase(status->alive_iter_);
        } else{
            mfu_.erase(status->alive_iter_);
        }

        mfu_.push_front(frame_id);

        status->alive_iter_ = mfu_.begin();
        status->arc_status_ = ArcStatus::MFU;
        return;
    } 
    // Case 2/3 : page_id found in a ghost list -> adapt target size, bring back to MFU
    auto ghost_it = ghost_map_.find(page_id);
    if(ghost_it != ghost_map_.end()){
        auto &ghost_status = ghost_it->second;
        size_t mru_ghost_size = mru_ghost_.size();
        size_t mfu_ghost_size = mfu_ghost_.size();

        if(ghost_status->arc_status_ == ArcStatus::MRU_GHOST){
            if(mru_ghost_size >= mfu_ghost_size){
                mru_target_size_ = std::min(mru_target_size_ + 1, replacer_size_);
            }else{
                mru_target_size_ = std::min(mru_target_size_ + mfu_ghost_size / mru_ghost_size, replacer_size_);
            }
            mru_ghost_.erase(ghost_status->ghost_iter_);
        }else{
            if(mfu_ghost_size >= mru_ghost_size){
                mru_target_size_ = (mru_target_size_ > 0) ? mru_target_size_ - 1 : 0;
            }
            mfu_ghost_.erase(ghost_status->ghost_iter_);
        }
        ghost_map_.erase(ghost_it);

        mfu_.push_front(frame_id);
        auto new_status = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MFU);
        new_status->alive_iter_ = mfu_.begin();
        alive_map_[frame_id] = new_status;
        return;
    }

    // case 4: complete miss -> determine if a ghost entry must be trimmed before adding to MRU
    size_t mru_total = mru_.size() + mru_ghost_.size();
    if(mru_total == replacer_size_){
        if(!mru_ghost_.empty()){
            ghost_map_.erase(mru_ghost_.back());
            mru_ghost_.pop_back();
        }
    }else if(mru_total + mfu_.size() + mfu_ghost_.size() == 2 * replacer_size_){
        if(!mfu_ghost_.empty()){
            ghost_map_.erase(mfu_ghost_.back());
            mfu_ghost_.pop_back();
        }
    }

    mru_.push_front(frame_id);
    auto new_status = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MRU);
    new_status->alive_iter_ = mru_.begin();
    alive_map_[frame_id] = new_status;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
    std::lock_guard<std::mutex> lock(latch_);

    BUSTUB_ASSERT(frame_id >= 0, "invalid frame_id");

    auto it = alive_map_.find(frame_id);

    if(it == alive_map_.end()){
        return;
    }

    auto &status = it->second;

    if(status->evictable_ == set_evictable){
        return;
    }

    status->evictable_ = set_evictable;

    if(set_evictable){
        curr_size_++;
    }else{
        curr_size_--;
    }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto it = alive_map_.find(frame_id);
    if(it == alive_map_.end()){
        return;
    }

    auto &status = it->second;
      BUSTUB_ASSERT(status->evictable_, "ArcReplacer::Remove: attempt to remove a non-evictable frame");

  // Remove from the appropriate alive list
  if (status->arc_status_ == ArcStatus::MRU) {
    mru_.erase(status->alive_iter_);
  } else {
    mfu_.erase(status->alive_iter_);
  }

  alive_map_.erase(it);
  curr_size_--;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t { 
    std::lock_guard<std::mutex> lock(latch_);
    return curr_size_; 
}

}  // namespace bustub
