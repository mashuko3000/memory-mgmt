#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H

#include <allocator_guardant.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <logger_guardant.h>
#include <typename_holder.h>
#include <mutex>

class allocator_buddies_system final:
    private allocator_guardant,
    public allocator_test_utils,
    public allocator_with_fit_mode,
    private logger_guardant,
    private typename_holder
{

private:
    
    void *_trusted_memory;

public:
    
    ~allocator_buddies_system() override;
    
    allocator_buddies_system(
        allocator_buddies_system const &other) = delete;
    
    allocator_buddies_system &operator=(
        allocator_buddies_system const &other) = delete;
    
    allocator_buddies_system(
        allocator_buddies_system &&other) noexcept;
    
    allocator_buddies_system &operator=(
        allocator_buddies_system &&other) noexcept;

public:
    
    explicit allocator_buddies_system(
        size_t space_size_power_of_two,
        allocator *parent_allocator = nullptr,
        logger *logger = nullptr,
        allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

public:
    
    [[nodiscard]] void *allocate(
        size_t value_size,
        size_t values_count) override;
    
    void deallocate(
        void *at) override;

public:
    
    inline void set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) override;

private:
    
    inline allocator *get_allocator() const override;

public:
    
    std::vector<allocator_test_utils::block_info> get_blocks_info() const noexcept override;

private:

    inline logger *get_logger() const override;

private:
    
    inline std::string get_typename() const noexcept override;

private:
    inline size_t get_pow_of_two_from_size(size_t size) const;
    inline size_t get_size_of_data_block_from_pow(size_t pow) const;
    inline size_t get_size_of_main_meta() const;
    inline size_t get_reserved_block_metadata_size() const;
    inline size_t get_free_block_metadata_size() const;

private:
    inline allocator *&get_parent_allocator() const;
    inline logger *&get_log() const;
    inline size_t &get_memory_size() const;
    inline std::mutex &get_mutex() const;
    inline allocator_with_fit_mode::fit_mode &get_fit_mode() const;
    inline void *& get_first_available_block() const;

private:
    inline unsigned int &block_metadata(void *block) const;
    inline size_t block_pow(void *block) const;
    inline void block_pow(void *block, size_t pow);
    inline bool block_flag(void *block) const;
    inline void block_flag(void *block, bool flag);
    inline void *&block_next_ptr(void *block) const;
    inline void *&block_previous_ptr(void *block) const;
    inline void *&block_trusted_memory(void *block) const;
    void check_consistency() const;
    inline size_t &block_metadata_combined(void *block) const;
private:
    inline unsigned int &block_metadata_pow(void *block) const;
    inline unsigned int &block_metadata_flag(void *block) const;
    inline size_t get_block_size(void *block) const;
private:
    inline void* get_start() const;
    inline void* get_end() const;
    void* get_buddy_of_block(void* block);
    inline void set_first_free_block_address(void* pointer);
};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H
