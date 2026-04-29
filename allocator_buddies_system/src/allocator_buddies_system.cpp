#include <not_implemented.h>
#include <set>

#include "../include/allocator_buddies_system.h"

allocator_buddies_system::~allocator_buddies_system() {
    if (_trusted_memory == nullptr) {
        return;
    }
    allocator *parent = get_parent_allocator();
    if (parent != nullptr) {
        try {
            parent->deallocate(_trusted_memory);
        } catch (const std::exception &e) {
            auto *log = get_log();
            if (log != nullptr) {
                log->error("Failed to deallocate via parent allocator: " + std::string(e.what()));
            }
            error_with_guard("Failed to deallocate via parent allocator");
        }
    } else {
        ::operator delete(_trusted_memory);
    }
    _trusted_memory = nullptr;
}

allocator_buddies_system::allocator_buddies_system(
        allocator_buddies_system &&other) noexcept {
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
}

allocator_buddies_system &allocator_buddies_system::operator=(
        allocator_buddies_system &&other) noexcept {
    if (&other != this) {
        deallocate(_trusted_memory);
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

allocator_buddies_system::allocator_buddies_system(
        size_t space_size,
        allocator *parent_allocator,
        logger *logger,
        allocator_with_fit_mode::fit_mode allocate_fit_mode) {
    space_size = get_size_of_data_block_from_pow(space_size);

    if (space_size < get_size_of_main_meta()) {
        auto *log = logger;
        if (log != nullptr) {
            log->error("Space size power of two must be at least 8 (256 bytes)");
        }
        throw std::logic_error("Space size power of two must be at least 8 (256 bytes)");
    }

    if (space_size < get_size_of_main_meta()) {
        space_size = get_size_of_main_meta() * 2;
    }
    size_t pow_of_data_block_size = get_pow_of_two_from_size(space_size);
    size_t final_size_of_data_block = get_size_of_data_block_from_pow(pow_of_data_block_size);
    if (final_size_of_data_block < space_size) {
        final_size_of_data_block = get_size_of_data_block_from_pow(pow_of_data_block_size + 1);
        ++pow_of_data_block_size;
    }
    size_t data_block_size_with_main_meta_data = get_size_of_main_meta() + final_size_of_data_block;

    try {
        _trusted_memory =
                parent_allocator != nullptr ? parent_allocator->allocate(data_block_size_with_main_meta_data, 1)
                                            : ::operator new(data_block_size_with_main_meta_data);
    } catch (std::bad_alloc const &ex) {
        auto *log = get_log();
        if (log != nullptr) {
            log->error("Failed to allocate memory for buddy system");
        }
        error_with_guard("Failed to allocate memory for buddy system");
        throw;
    }

    get_parent_allocator() = parent_allocator;
    get_log() = logger;
    get_memory_size() = final_size_of_data_block;
    allocator::construct(&get_mutex());
    get_fit_mode() = allocate_fit_mode;
    get_first_available_block() = reinterpret_cast<unsigned char *>(_trusted_memory) + get_size_of_main_meta();

    void *first_available_block = get_first_available_block();
    block_pow(first_available_block, pow_of_data_block_size);
    block_flag(first_available_block, false);
    block_next_ptr(first_available_block) = nullptr;
    block_previous_ptr(first_available_block) = nullptr;

    auto *log = get_log();
    if (log != nullptr) {
        log->debug("Allocator initialized with space_size=" + std::to_string(space_size) +
                   ", pow=" + std::to_string(pow_of_data_block_size) +
                   ", final_size=" + std::to_string(final_size_of_data_block) +
                   ", first_block=" + std::to_string(reinterpret_cast<uintptr_t>(first_available_block)));
    }
}

[[nodiscard]] void *allocator_buddies_system::allocate(
        size_t value_size,
        size_t values_count) {
    std::lock_guard<std::mutex> guard(get_mutex());

    size_t required_data_size = value_size * values_count + get_reserved_block_metadata_size();
    if (required_data_size < get_free_block_metadata_size()) {
        required_data_size = get_free_block_metadata_size();
    }
    size_t required_pow_size = get_pow_of_two_from_size(required_data_size);

    size_t max_pow = (1U << (CHAR_BIT - 1)) - 1;
    if (required_pow_size > max_pow) {
        if (get_log() != nullptr) {
            get_log()->error("Requested block size too large for buddy system");
        }
        error_with_guard("Requested block size too large for buddy system");
        throw std::bad_alloc();
    }

    allocator_with_fit_mode::fit_mode fit_mode = get_fit_mode();

    void *current_block = get_first_available_block();
    if (current_block == nullptr || current_block < get_start() || current_block >= get_end()) {
        if (get_log() != nullptr) {
            get_log()->error("Invalid first available block: " + std::to_string(reinterpret_cast<uintptr_t>(current_block)));
        }
        throw std::runtime_error("Invalid first available block");
    }

    void *previous_block = nullptr;
    void *current_target = nullptr;
    size_t current_pow = 0;

    auto *log = get_log();
    if (log != nullptr) {
        log->debug("Starting allocation: required_size=" + std::to_string(required_data_size) +
                   ", required_pow=" + std::to_string(required_pow_size) +
                   ", current_block=" + std::to_string(reinterpret_cast<uintptr_t>(current_block)) +
                   ", start=" + std::to_string(reinterpret_cast<uintptr_t>(get_start())) +
                   ", end=" + std::to_string(reinterpret_cast<uintptr_t>(get_end())));
    }

    while (current_block != nullptr) {
        size_t block_pow_ = block_pow(current_block);
        if (block_pow_ < required_pow_size) {
            previous_block = current_block;
            current_block = block_next_ptr(current_block);
            continue;
        }

        switch (fit_mode) {
            case allocator_with_fit_mode::fit_mode::first_fit:
                current_target = current_block;
                current_pow = block_pow_;
                current_block = nullptr;
                break;
            case allocator_with_fit_mode::fit_mode::the_best_fit:
                if (current_target == nullptr || block_pow_ < current_pow) {
                    current_target = current_block;
                    current_pow = block_pow_;
                }
                if (block_pow_ == required_pow_size) {
                    current_block = nullptr;
                } else {
                    previous_block = current_block;
                    current_block = block_next_ptr(current_block);
                }
                break;
            case allocator_with_fit_mode::fit_mode::the_worst_fit:
                if (current_target == nullptr || block_pow_ > current_pow) {
                    current_target = current_block;
                    current_pow = block_pow_;
                }
                previous_block = current_block;
                current_block = block_next_ptr(current_block);
                break;
        }
    }

    if (current_target == nullptr) {
        if (get_log() != nullptr) {
            get_log()->error("No suitable block found for allocation");
        }
        error_with_guard("No suitable block found for allocation");
        throw std::bad_alloc();
    }

    void *curr_block = current_target;
    while (current_pow > required_pow_size) {
        --current_pow;
        block_pow(curr_block, current_pow);
        if (log != nullptr) {
            log->debug("Split block: address=" + std::to_string(reinterpret_cast<uintptr_t>(curr_block)) +
                       ", new_pow=" + std::to_string(current_pow) +
                       ", actual_pow=" + std::to_string(block_pow(curr_block)));
        }
        void *buddy_block = reinterpret_cast<unsigned char *>(curr_block) + get_block_size(curr_block);

        block_flag(buddy_block, false);
        block_pow(buddy_block, current_pow);
        if (log != nullptr) {
            log->debug("Buddy block: address=" + std::to_string(reinterpret_cast<uintptr_t>(buddy_block)) +
                       ", pow=" + std::to_string(current_pow) +
                       ", actual_pow=" + std::to_string(block_pow(buddy_block)));
        }
        block_next_ptr(buddy_block) = block_next_ptr(curr_block);
        block_previous_ptr(buddy_block) = curr_block;

        if (block_next_ptr(curr_block) != nullptr) {
            block_previous_ptr(block_next_ptr(curr_block)) = buddy_block;
        }
        block_next_ptr(curr_block) = buddy_block;
    }

    block_flag(current_target, true);
    block_trusted_memory(current_target) = _trusted_memory;

    if (block_next_ptr(current_target) != nullptr) {
        block_previous_ptr(block_next_ptr(current_target)) = block_previous_ptr(current_target);
    }
    if (block_previous_ptr(current_target) != nullptr) {
        block_next_ptr(block_previous_ptr(current_target)) = block_next_ptr(current_target);
    } else {
        get_first_available_block() = block_next_ptr(current_target);
        if (log != nullptr) {
            log->debug("Updated first available block: " + std::to_string(reinterpret_cast<uintptr_t>(get_first_available_block())));
        }
    }

    void *result = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(current_target) +
                                            get_reserved_block_metadata_size());
    if (log != nullptr) {
        log->debug("Allocated block: address=" + std::to_string(reinterpret_cast<uintptr_t>(result)) +
                   ", size=" + std::to_string(value_size * values_count) +
                   ", target_block=" + std::to_string(reinterpret_cast<uintptr_t>(current_target)));
    }
    check_consistency();
    return result;
}

void allocator_buddies_system::deallocate(void *at) {
    if (at == nullptr) {
        if (get_log() != nullptr) {
            get_log()->error("Attempt to deallocate null pointer");
        }
        error_with_guard("Attempt to deallocate null pointer");
        throw std::invalid_argument("Null pointer deallocation");
    }

    std::lock_guard<std::mutex> guard(get_mutex());

    size_t reserved_metadata_size = get_reserved_block_metadata_size();
    void *target_block = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(at) - reserved_metadata_size);

    auto *log = get_log();
    if (log != nullptr) {
        log->debug("Deallocating block: at=" + std::to_string(reinterpret_cast<uintptr_t>(at)) +
                   ", target_block=" + std::to_string(reinterpret_cast<uintptr_t>(target_block)) +
                   ", start=" + std::to_string(reinterpret_cast<uintptr_t>(get_start())) +
                   ", end=" + std::to_string(reinterpret_cast<uintptr_t>(get_end())) +
                   ", trusted=" + std::to_string(reinterpret_cast<uintptr_t>(_trusted_memory)) +
                   ", block_trusted=" +
                   std::to_string(reinterpret_cast<uintptr_t>(block_trusted_memory(target_block))));
    }

    if (target_block < get_start() || target_block >= get_end() ||
        block_trusted_memory(target_block) != _trusted_memory) {
        if (log != nullptr) {
            log->error("Invalid block for deallocation: target_block=" +
                       std::to_string(reinterpret_cast<uintptr_t>(target_block)) +
                       ", start=" + std::to_string(reinterpret_cast<uintptr_t>(get_start())) +
                       ", end=" + std::to_string(reinterpret_cast<uintptr_t>(get_end())) +
                       ", trusted=" + std::to_string(reinterpret_cast<uintptr_t>(_trusted_memory)) +
                       ", block_trusted=" +
                       std::to_string(reinterpret_cast<uintptr_t>(block_trusted_memory(target_block))));
        }
        error_with_guard("Invalid block for deallocation");
        throw std::invalid_argument("Invalid block for deallocation");
    }

    if (!block_flag(target_block)) {
        if (log != nullptr) {
            log->error("Block is already free");
        }
        error_with_guard("Block is already free");
        throw std::invalid_argument("Block is already free");
    }

    size_t max_pow = get_pow_of_two_from_size(get_memory_size());
    size_t max_allowed_pow = (1U << (sizeof(size_t) * CHAR_BIT - 1)) - 1;

    if (max_pow > max_allowed_pow) {
        max_pow = max_allowed_pow;
    }

    block_flag(target_block, false);
    block_next_ptr(target_block) = nullptr;
    block_previous_ptr(target_block) = nullptr;

    while (block_pow(target_block) < max_pow) {
        void *buddy_block = get_buddy_of_block(target_block);
        size_t target_block_pow = block_pow(target_block);

        if (buddy_block < get_start() || buddy_block >= get_end() ||
            block_pow(buddy_block) != target_block_pow || block_flag(buddy_block)) {
            break;
        }

        if (block_next_ptr(buddy_block) != nullptr) {
            block_previous_ptr(block_next_ptr(buddy_block)) = block_previous_ptr(buddy_block);
        }

        if (block_previous_ptr(buddy_block) != nullptr) {
            block_next_ptr(block_previous_ptr(buddy_block)) = block_next_ptr(buddy_block);
        } else {
            set_first_free_block_address(block_next_ptr(buddy_block));
        }

        void *left_block = target_block < buddy_block ? target_block : buddy_block;
        block_pow(left_block, target_block_pow + 1);
        block_next_ptr(left_block) = nullptr;
        block_previous_ptr(left_block) = nullptr;
        target_block = left_block;
    }

    void *next_block = get_first_available_block();
    void *previous_block = nullptr;

    while (next_block != nullptr && next_block < target_block) {
        previous_block = next_block;
        next_block = block_next_ptr(next_block);
    }

    block_next_ptr(target_block) = next_block;
    block_previous_ptr(target_block) = previous_block;

    if (next_block != nullptr) {
        block_previous_ptr(next_block) = target_block;
    }
    if (previous_block != nullptr) {
        block_next_ptr(previous_block) = target_block;
    } else {
        set_first_free_block_address(target_block);
    }

    check_consistency();
}

inline void allocator_buddies_system::set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) {
    get_fit_mode() = mode;
}

inline allocator *allocator_buddies_system::get_allocator() const {
    return get_parent_allocator();
}

std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info() const noexcept {
    debug_with_guard(
            "Called method std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info() const noexcept");

    std::vector<allocator_test_utils::block_info> state;
    void *current_block = get_start();
    void *end = get_end();

    auto *log = get_log();
    if (log != nullptr) {
        log->debug("Analyzing memory: start=" + std::to_string(reinterpret_cast<uintptr_t>(current_block)) +
                   ", end=" + std::to_string(reinterpret_cast<uintptr_t>(end)));
    }

    std::set<void *> free_blocks;
    void *free_block = get_first_available_block();
    while (free_block != nullptr && free_block >= get_start() && free_block < end) {
        free_blocks.insert(free_block);
        void *next_block = block_next_ptr(free_block);
        if (log != nullptr) {
            log->debug("Free block at address=" + std::to_string(reinterpret_cast<uintptr_t>(free_block)));
        }
        if (next_block <= free_block || next_block >= end) {
            debug_with_guard(
                    "Invalid next block address at " + std::to_string(reinterpret_cast<uintptr_t>(next_block)));
            break;
        }
        free_block = next_block;
    }

    if (current_block == end) {
        debug_with_guard("No memory region to analyze");
        return state;
    }

    while (current_block < end) {
        if (free_blocks.empty() && !block_flag(current_block)) {
            size_t remaining_size =
                    reinterpret_cast<unsigned char *>(end) - reinterpret_cast<unsigned char *>(current_block);
            if (log != nullptr) {
                log->debug("Remaining block: size=" + std::to_string(remaining_size));
            }
            state.push_back({remaining_size, false});
            break;
        }

        bool is_free = free_blocks.find(current_block) != free_blocks.end();
        if (is_free != !block_flag(current_block)) {
            debug_with_guard("Inconsistency: block flag mismatch (is_free=" + std::to_string(is_free) +
                             ", flag=" + std::to_string(block_flag(current_block)) + ")");
            break;
        }

        size_t block_size = get_block_size(current_block);
        if (block_size == 0) {
            debug_with_guard(
                    "Zero block size at address=" + std::to_string(reinterpret_cast<uintptr_t>(current_block)) +
                    ", pow=" + std::to_string(block_pow(current_block)));
            break;
        }
        if (reinterpret_cast<unsigned char *>(current_block) + block_size > reinterpret_cast<unsigned char *>(end)) {
            debug_with_guard("Block size exceeds memory bounds: size=" + std::to_string(block_size));
            break;
        }

        if (log != nullptr) {
            log->debug("Block: address=" + std::to_string(reinterpret_cast<uintptr_t>(current_block)) +
                       ", size=" + std::to_string(block_size) +
                       ", is_free=" + std::to_string(is_free));
        }
        allocator_test_utils::block_info current_info{block_size, !is_free};
        state.push_back(current_info);

        current_block = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(current_block) + block_size);
    }
    state.shrink_to_fit();
    return state;
}

inline logger *allocator_buddies_system::get_logger() const {
    return get_log();
}

inline std::string allocator_buddies_system::get_typename() const noexcept {
    return "buddy allocator";
}

inline size_t &allocator_buddies_system::block_metadata_combined(void *block) const {
    return *reinterpret_cast<size_t *>(block);
}

inline size_t allocator_buddies_system::block_pow(void *block) const {
    return block_metadata_combined(block) >> 1;
}

inline void allocator_buddies_system::block_pow(void *block, size_t pow) {
    size_t max_pow = (1U << (sizeof(size_t) * CHAR_BIT - 1 - 1)) - 1;
    if (pow > max_pow) {
        logger *log = get_log();
        if (log != nullptr) {
            log->error("Block power exceeds maximum allowed value");
        } else {
            error_with_guard("Block power exceeds maximum allowed value (no logger)");
        }
        throw std::invalid_argument("Block power too large");
    }
    size_t flag = block_metadata_combined(block) & 1;
    block_metadata_combined(block) = ((static_cast<size_t>(pow) << 1) | flag);
}

inline bool allocator_buddies_system::block_flag(void *block) const {
    return block_metadata_combined(block) & 1;
}

inline void allocator_buddies_system::block_flag(void *block, bool flag) {
    size_t pow = block_metadata_combined(block) >> 1;
    block_metadata_combined(block) = (pow << 1) | (flag ? 1U : 0U);
    logger *log = get_log();
    if (log != nullptr && flag) {
        log->debug("Block flagged as occupied");
    }
}

inline void *&allocator_buddies_system::block_trusted_memory(void *block) const {
    return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(block) + sizeof(size_t));
}

inline void *&allocator_buddies_system::block_next_ptr(void *block) const {
    return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(block) + sizeof(size_t) + sizeof(void *));
}

inline void *&allocator_buddies_system::block_previous_ptr(void *block) const {
    return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(block) + sizeof(size_t) + 2 * sizeof(void *));
}

inline size_t allocator_buddies_system::get_reserved_block_metadata_size() const {
    return sizeof(size_t) + sizeof(void *);
}

inline size_t allocator_buddies_system::get_free_block_metadata_size() const {
    return sizeof(size_t) + 2 * sizeof(void *);
}

inline size_t allocator_buddies_system::get_block_size(void *block) const {
    return get_size_of_data_block_from_pow(block_pow(block));
}

inline size_t allocator_buddies_system::get_pow_of_two_from_size(size_t size) const {
    if (size == 0) return 0;
    size_t pow = 0;
    size_t temp = size - 1;
    while (temp > 0) {
        temp >>= 1;
        ++pow;
    }
    return (size_t(1) << pow < size) ? pow + 1 : pow;
}

inline size_t allocator_buddies_system::get_size_of_data_block_from_pow(size_t pow) const {
    return (pow == 0) ? 0 : (size_t(1) << pow);
}

inline size_t allocator_buddies_system::get_size_of_main_meta() const {
    return sizeof(allocator *) + sizeof(logger *) + sizeof(size_t) + sizeof(std::mutex) +
           sizeof(allocator_with_fit_mode::fit_mode) +
           sizeof(void *);
}

inline allocator *&allocator_buddies_system::get_parent_allocator() const {
    return *reinterpret_cast<allocator **>(_trusted_memory);
}

inline logger *&allocator_buddies_system::get_log() const {
    return *reinterpret_cast<logger **>(&get_parent_allocator() + 1);
}

inline size_t &allocator_buddies_system::get_memory_size() const {
    return *reinterpret_cast<size_t *>(&get_log() + 1);
}

inline std::mutex &allocator_buddies_system::get_mutex() const {
    return *reinterpret_cast<std::mutex *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(size_t) + sizeof(allocator *) + sizeof(logger*));
}

inline allocator_with_fit_mode::fit_mode &allocator_buddies_system::get_fit_mode() const {
    return *reinterpret_cast<allocator_with_fit_mode::fit_mode *>(&get_mutex() + 1);
}

inline void *&allocator_buddies_system::get_first_available_block() const {
    return *reinterpret_cast<void **>(&get_fit_mode() + 1);
}

inline void *allocator_buddies_system::get_start() const {
    return reinterpret_cast<unsigned char *>(_trusted_memory) + get_size_of_main_meta();
}

inline void *allocator_buddies_system::get_end() const {
    return reinterpret_cast<unsigned char *>(get_start()) + get_memory_size();
}

void *allocator_buddies_system::get_buddy_of_block(void *block) {
    size_t block_size = get_block_size(block);
    size_t offset = reinterpret_cast<unsigned char *>(block) - reinterpret_cast<unsigned char *>(get_start());
    size_t result = offset ^ block_size;
    return reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(get_start()) + result);
}

inline void allocator_buddies_system::set_first_free_block_address(void *pointer) {
    *reinterpret_cast<void **>(
            reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) + sizeof(logger *) +
            sizeof(size_t) + sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode)) = pointer;
}

void allocator_buddies_system::check_consistency() const {
    auto *log = get_log();
    void *current_block = get_start();
    void *end = get_end();

    while (current_block < end) {
        size_t block_size = get_block_size(current_block);
        if (block_size == 0) {
            if (log != nullptr) {
                log->error("Consistency check failed: zero block size at address=" +
                           std::to_string(reinterpret_cast<uintptr_t>(current_block)));
            }
            throw std::runtime_error("Consistency check failed: zero block size");
        }
        if (reinterpret_cast<unsigned char *>(current_block) + block_size > reinterpret_cast<unsigned char *>(end)) {
            if (log != nullptr) {
                log->error("Consistency check failed: block exceeds memory bounds at address=" +
                           std::to_string(reinterpret_cast<uintptr_t>(current_block)));
            }
            throw std::runtime_error("Consistency check failed: block exceeds memory bounds");
        }
        current_block = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(current_block) + block_size);
    }

    if (log != nullptr) {
        log->debug("Consistency check passed");
    }
}