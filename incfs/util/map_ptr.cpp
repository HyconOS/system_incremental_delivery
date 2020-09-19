/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <utils/FileMap.h>

#ifdef __ANDROID__
#include "incfs_inline.h"
#endif

#include "util/map_ptr.h"

namespace android::incfs::util {

IncFsFileMap::IncFsFileMap() = default;
IncFsFileMap::~IncFsFileMap() = default;

const void* IncFsFileMap::unsafe_data() const {
    return map_->getDataPtr();
}

size_t IncFsFileMap::length() const {
    return map_->getDataLength();
}

off64_t IncFsFileMap::offset() const {
    return map_->getDataOffset();
}

const char* IncFsFileMap::file_name() const {
    return map_->getFileName();
}

#ifndef __ANDROID__
bool IncFsFileMap::Create(int fd, off64_t offset, size_t length, const char* debug_file_name) {
    if (fd < 0) return false;
    map_ = std::make_unique<android::FileMap>();
    return map_->create(debug_file_name, fd, offset, length, true /* readOnly */);
}

bool IncFsFileMap::IsVerificationEnabled() const {
    return false;
}

#else
using data_block_index_t = uint32_t;

data_block_index_t get_block_index(const uint8_t* ptr, const uint8_t* start_block_ptr) {
    return (ptr - start_block_ptr) / INCFS_DATA_FILE_BLOCK_SIZE;
}

bool IncFsFileMap::Create(int fd, off64_t offset, size_t length, const char* file_name) {
    map_ = std::make_unique<android::FileMap>();
    if (!map_->create(file_name, fd, offset, length, true /* readOnly */)) {
        return false;
    }

    // Initialize the block cache with enough buckets to hold all of the blocks within the
    // memory-mapped region.
    fd_ = fd;
    size_t offset_diff = offset % INCFS_DATA_FILE_BLOCK_SIZE;
    size_t base_length_ = length + offset_diff;
    start_block_offset_ = offset - offset_diff;
    start_block_ptr_ = reinterpret_cast<const uint8_t*>(map_->getDataPtr()) - offset_diff;

    const size_t bucket_count = (base_length_ / INCFS_DATA_FILE_BLOCK_SIZE) / kBucketBits;
    loaded_blocks_ = std::vector<std::atomic<bucket_t> >(bucket_count + 1U);
    return true;
}

bool IncFsFileMap::IsVerificationEnabled() const {
    return isIncFsFd(fd_) && isFullyLoaded(fd_) != LoadingState::Full;
}

bool IncFsFileMap::Verify(const uint8_t* const& data_start, const uint8_t* const& data_end,
                          const uint8_t** prev_verified_block) const {
    const data_block_index_t start_index = get_block_index(data_start, start_block_ptr_);
    const data_block_index_t end_index = get_block_index(data_end - 1U, start_block_ptr_);

    bool success = true;
    // Retrieve the set of the required blocks that must be present in order to read the data
    // safely.
    for (data_block_index_t curr_index = start_index; curr_index <= end_index; ++curr_index) {
        const size_t i = curr_index / kBucketBits;
        const auto present_bit = 1U << (curr_index % kBucketBits);
        std::atomic<bucket_t>& bucket = loaded_blocks_[i];
        if (LIKELY(bucket.load(std::memory_order_relaxed) & present_bit)) {
            continue;
        }

        // Touch all of the blocks with pread to ensure that the region of data is fully present.
        uint8_t value;
        const off64_t read_offset = (curr_index * INCFS_DATA_FILE_BLOCK_SIZE) + start_block_offset_;
        if (UNLIKELY(!android::base::ReadFullyAtOffset(fd_, &value, 1U, read_offset))) {
            success = false;
            break;
        }

        bucket.fetch_or(present_bit, std::memory_order_relaxed);
    }

    if (UNLIKELY(!success)) {
        // Log the region of the file that could not be fully loaded.
        size_t start_offset = (data_start - start_block_ptr_) + map_->getDataOffset();
        size_t end_offset = (data_end - start_block_ptr_) + map_->getDataOffset();
        std::string location = file_name() ? base::StringPrintf("path %s", file_name())
                                           : base::StringPrintf("fd %d", fd_);
        const std::string message =
                base::StringPrintf("region 0x%016zx - 0x%016zx of %s not fully loaded",
                                   start_offset, end_offset, location.c_str());
        LOG(WARNING) << message;
        return false;
    }

    // Update the previous verified block pointer to optimize repeated verifies on the same block.
    *prev_verified_block = start_block_ptr_ + (end_index * INCFS_DATA_FILE_BLOCK_SIZE);
    return true;
}
#endif

} // namespace android::incfs::util