#include <not_implemented.h>

#include "../include/allocator_boundary_tags.h"

void allocator_boundary_tags::deallocate_object_fields()
{
    auto* logger = get_logger();
    if (logger != nullptr) logger->trace("Called method void allocator_boundary_tags::deallocate_object_fields()");

    if (_trusted_memory != nullptr)
    {
        allocator* a = get_allocator();
        if (a != nullptr)
            a->deallocate(_trusted_memory);
        else
            ::delete a;

        _trusted_memory = nullptr;
    }

    if (logger != nullptr) logger->trace("Successfully executed method void allocator_boundary_tags::deallocate_object_fields()");
}

allocator_boundary_tags::~allocator_boundary_tags() {
    auto *logger = get_logger();
    if (logger != nullptr) logger->debug("Called method allocator_boundary_tags::~allocator_boundary_tags()");
    deallocate_object_fields();
    if (logger != nullptr)
        logger->debug("Successfully executed method allocator_boundary_tags::~allocator_boundary_tags()");
}

allocator_boundary_tags::allocator_boundary_tags(
        allocator_boundary_tags const &other) {
    throw not_implemented("allocator_boundary_tags::allocator_boundary_tags(allocator_boundary_tags const &)",
                          "your code should be here...");
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
        allocator_boundary_tags const &other) {
    throw not_implemented(
            "allocator_boundary_tags &allocator_boundary_tags::operator=(allocator_boundary_tags const &)",
            "your code should be here...");
}

allocator_boundary_tags::allocator_boundary_tags(
        allocator_boundary_tags &&other) noexcept {
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;

    debug_with_guard(
            "Called method allocator_boundary_tags::allocator_boundary_tags(allocator_boundary_tags && other) noexcept");
    debug_with_guard(
            "Successfully executed method allocator_boundary_tags::allocator_boundary_tags(allocator_boundary_tags && other) noexcept");
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
        allocator_boundary_tags &&other) noexcept {
    debug_with_guard(
            "Called method allocator_boundary_tags& allocator_boundary_tags::operator=(allocator_boundary_tags && other) noexcept");

    if (&other != this) {
        deallocate_object_fields();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }

    debug_with_guard(
            "Successfully executed method allocator_boundary_tags& allocator_boundary_tags::operator=(allocator_boundary_tags && other) noexcept");
    return *this;
}

allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        allocator *parent_allocator,
        logger *logger,
        allocator_with_fit_mode::fit_mode allocate_fit_mode) {
    size_t size_with_meta = space_size + get_allocator_metadata_size();

    try {
        _trusted_memory = parent_allocator != nullptr
                          ? parent_allocator->allocate(size_with_meta, 1)
                          : ::operator new(size_with_meta);
    }
    catch (const std::bad_alloc &e) {
        if (logger != nullptr) logger->error(std::string("Failed to allocate") + e.what());
        throw;
    }

    get_parent_allocator() = parent_allocator;
    get_log() = logger;
    get_memory_size() = space_size;
    allocator::construct(&get_mutex());
    get_fit_mode() = allocate_fit_mode;
    get_first_occupied_block() = nullptr;
    get_available_size() = space_size;
}

[[nodiscard]] void *allocator_boundary_tags::allocate(
        size_t value_size,
        size_t values_count) {
    size_t required_size = value_size * values_count + get_occupied_block_metadata_size();
    if (required_size > get_available_size()) {
        error_with_guard("cant allocate memory");
    }
    allocator_with_fit_mode::fit_mode fit_mode = get_fit_mode();
    void *memory_start = get_start();
    void *memory_end = get_end();
    size_t size_of_user_data;
    size_t optimal = fit_mode == allocator_with_fit_mode::fit_mode::the_best_fit
                     ? get_memory_size() + 1
                     : -1;

    void *current = get_first_occupied_block();
    void *previous_block = nullptr;
    void *current_target = nullptr;
    void *previous_target = nullptr;

    if (current == nullptr) {
        previous_block = nullptr;
        current_target = nullptr;
        optimal = get_memory_size();
    } else if (memory_start != current) {
        size_of_user_data =
                reinterpret_cast<unsigned char *>(current) - reinterpret_cast<unsigned char *>(memory_start);
        if (size_of_user_data >= required_size) {
            previous_block = nullptr;
            current_target = current;
            optimal = size_of_user_data;
            if (fit_mode == allocator_with_fit_mode::fit_mode::first_fit) current = nullptr;
        }
    }
    while (current != nullptr) {
        previous_block = current;
        current = get_next_occupied_block(current);

        size_of_user_data = reinterpret_cast<unsigned char *>(current != nullptr ? current : memory_end) -
                            reinterpret_cast<unsigned char *>(previous_block) -
                            get_size_occupied_block(previous_block) -
                            get_occupied_block_metadata_size();
        if (size_of_user_data >= required_size) {
            bool fit = false;
            bool stop = false;
            switch (fit_mode) {
                case allocator_with_fit_mode::fit_mode::first_fit:
                    fit = true;
                    stop = true;
                    break;
                case allocator_with_fit_mode::fit_mode::the_best_fit:
                    fit = size_of_user_data <= optimal;
                    if (size_of_user_data == optimal) stop = true;
                    break;
                case allocator_with_fit_mode::fit_mode::the_worst_fit:
                    fit = size_of_user_data >= optimal;
                    break;
            }
            if (fit) {
                optimal = size_of_user_data;
                current_target = current;
                previous_target = previous_block;
                if (stop) break;
            }
        }
    }

    if (optimal == -1 || optimal == get_memory_size() + 1)
    {
        error_with_guard(get_typename() + ": can't allocate memory - no occupied memory");
        debug_with_guard("Cancel with error method [[nodiscard]] void* allocator_sorted_list::allocate(size_t value_size, size_t values_count): can't allocate memory - no occupied memory");
        throw std::bad_alloc();
    }

    size_t right_block_size = optimal - required_size;
    void *cur;
    if (right_block_size <= (get_occupied_block_metadata_size() + 8)) {
        cur = add_finded_block_to_occupied_list(previous_target, current_target, required_size - get_occupied_block_metadata_size());
        get_available_size() - optimal;
    }
    else
    {
        cur = add_finded_block_to_occupied_list(previous_target, current_target, required_size - get_occupied_block_metadata_size());
        get_available_size() - required_size;
    }
    return reinterpret_cast<void*>(reinterpret_cast<unsigned char*>(cur) + get_occupied_block_metadata_size());
}

void allocator_boundary_tags::deallocate(
        void *at) {
    std::lock_guard<std::mutex> lock(get_mutex());

    if (at == nullptr) {
        throw std::logic_error(get_typename() + ": null pointer");
    }

    size_t metadata_size = get_occupied_block_metadata_size();
    void *target_block = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(at) - metadata_size);

    if (target_block < get_start() || target_block > get_end() ||
        get_trusted_memory_occupied_block(target_block) != _trusted_memory) {
        error_with_guard(
                get_typename() + ": can't deallocate memory - the pointer referenced an invalid memory location");
        debug_with_guard(
                "Cancel with error method: void allocator_sorted_list::deallocate(void* at): can't deallocate memory - the pointer referenced an invalid memory locationr");
        throw std::logic_error(
                get_typename() + ": can't deallocate memory - the pointer referenced an invalid memory location");
    }

    get_available_size() = get_size_occupied_block(target_block) + metadata_size;

    void *previous_block = get_prev_occupied_block(target_block);
    void *next_block = get_next_occupied_block(target_block);

    if (previous_block != nullptr) get_next_occupied_block(previous_block) = next_block;
    else get_first_occupied_block() = next_block;

    if (next_block != nullptr) get_prev_occupied_block(next_block) = previous_block;


    debug_with_guard("Successfully executed method: void allocator_sorted_list::deallocate(void* at)");
}

inline void allocator_boundary_tags::set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) {
    get_fit_mode() = mode;
}

inline allocator *allocator_boundary_tags::get_allocator() const {
    return get_parent_allocator();
}


std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const noexcept {

    std::vector<allocator_test_utils::block_info> blocks_info;

    void *current = get_start();
    void *memory_end = get_end();

    allocator_boundary_tags* non_const_this = const_cast<allocator_boundary_tags*>(this);

    if (get_first_occupied_block() == nullptr)
    {
        if (reinterpret_cast<unsigned char*>(current) < reinterpret_cast<unsigned char*>(memory_end))
        {
            size_t free_block_size = reinterpret_cast<unsigned char*>(memory_end) - reinterpret_cast<unsigned char*>(current);
            blocks_info.push_back({ free_block_size, false });
        }
        return blocks_info;
    }

    void *occupied_block = get_first_occupied_block();
    while (occupied_block != nullptr)
    {
        if (current < occupied_block)
        {
            size_t gap_size = reinterpret_cast<unsigned char*>(occupied_block) - reinterpret_cast<unsigned char*>(current);
            blocks_info.push_back({ gap_size, false });
            current = occupied_block;
        }

        size_t user_block_size = non_const_this->get_size_occupied_block(occupied_block);
        size_t block_total_size = non_const_this->get_occupied_block_metadata_size() + user_block_size;
        blocks_info.push_back({block_total_size, true});

        current = reinterpret_cast<unsigned char*>(occupied_block) + block_total_size;

        void* next_block = non_const_this->get_next_occupied_block(occupied_block);
        occupied_block = next_block;
    }

    if (current < memory_end)
    {
        size_t gap_size = reinterpret_cast<unsigned char*>(memory_end) - reinterpret_cast<unsigned char*>(current);
        blocks_info.push_back({ gap_size, false });
    }

    return blocks_info;
}

inline logger *allocator_boundary_tags::get_logger() const {
    return get_log();
}

inline std::string allocator_boundary_tags::get_typename() const noexcept {
    return std::string("allocator boundary system");
}

void *allocator_boundary_tags::add_finded_block_to_occupied_list(void *previous_block, void *next_block, size_t size) {
    debug_with_guard(
            "Called method void * allocator_boundary_tags::add_finded_block_to_occupied_list(void * previous_block, void * next_block, size_t size)");

    void *current = previous_block == nullptr
                    ? get_start()
                    : reinterpret_cast<unsigned char *>(previous_block) + get_size_occupied_block(previous_block) +
                      get_occupied_block_metadata_size();

    if (next_block != nullptr) get_prev_occupied_block(next_block) = current;
    if (previous_block != nullptr) get_next_occupied_block(previous_block) = current;
    else get_first_occupied_block() = current;

    get_size_occupied_block(current) = size;
    get_next_occupied_block(current) = next_block;
    get_prev_occupied_block(current) = previous_block;
    get_trusted_memory_occupied_block(current) = (_trusted_memory);


    debug_with_guard(
            "Successfully executed method void * allocator_boundary_tags::add_finded_block_to_occupied_list(void * previous_block, void * next_block, size_t size)");
    return current;
}


size_t allocator_boundary_tags::get_occupied_block_metadata_size() const {
    return sizeof(size_t) + sizeof(void *) * 3;
}

inline size_t &allocator_boundary_tags::get_size_occupied_block(void *current) {
    return *reinterpret_cast<size_t *>(current);
}

inline void *&allocator_boundary_tags::get_prev_occupied_block(void *current) {
    return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(current) + sizeof(size_t)
    );
}

inline void *&allocator_boundary_tags::get_next_occupied_block(void *current) {
    return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(current) + sizeof(size_t) + sizeof(void*)
    );
}

inline void *&allocator_boundary_tags::get_trusted_memory_occupied_block(void *current) {
    return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(current) + sizeof(size_t) + 2 * sizeof(void*)
    );
}


size_t allocator_boundary_tags::get_allocator_metadata_size() const {
    return sizeof(allocator *) + sizeof(logger *) + sizeof(size_t) + sizeof(std::mutex) +
           sizeof(allocator_with_fit_mode::fit_mode) + sizeof(void *) + sizeof(size_t);
}

inline allocator *&allocator_boundary_tags::get_parent_allocator() const {
    return *reinterpret_cast<allocator **>(_trusted_memory);
}

inline logger *&allocator_boundary_tags::get_log() const {
    return *reinterpret_cast<logger **>(reinterpret_cast<unsigned char*>(_trusted_memory) + sizeof(allocator*));
}

inline size_t &allocator_boundary_tags::get_memory_size() const {
    return *reinterpret_cast<size_t *>(
            reinterpret_cast<unsigned char*>(_trusted_memory) +
            sizeof(allocator*) + sizeof(logger*)
    );
}

inline std::mutex &allocator_boundary_tags::get_mutex() const {
    return *reinterpret_cast<std::mutex *>(
            reinterpret_cast<unsigned char*>(_trusted_memory) +
            sizeof(allocator*) + sizeof(logger*) + sizeof(size_t)
    );
}

inline allocator_with_fit_mode::fit_mode &allocator_boundary_tags::get_fit_mode() const {
    return *reinterpret_cast<allocator_with_fit_mode::fit_mode *>(
            reinterpret_cast<unsigned char*>(_trusted_memory) +
            sizeof(allocator*) + sizeof(logger*) + sizeof(size_t) + sizeof(std::mutex)
    );
}

inline void *&allocator_boundary_tags::get_first_occupied_block() const {
    return *reinterpret_cast<void **>(
            reinterpret_cast<unsigned char*>(_trusted_memory) +
            sizeof(allocator*) + sizeof(logger*) + sizeof(size_t) +
            sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode)
    );
}

inline size_t &allocator_boundary_tags::get_available_size() const {
    return *reinterpret_cast<size_t *>(
            reinterpret_cast<unsigned char*>(_trusted_memory) +
            sizeof(allocator*) + sizeof(logger*) + sizeof(size_t) +
            sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(void*)
    );
}


inline void *allocator_boundary_tags::get_start() const {
    return reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(_trusted_memory) + get_allocator_metadata_size());
}

inline void *allocator_boundary_tags::get_end() const {
    return reinterpret_cast<unsigned char *>(get_start()) + get_memory_size();
}

















