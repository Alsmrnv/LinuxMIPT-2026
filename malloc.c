#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>


#define MMAP_THRESHOLD (128UL * 1024UL)
#define HEAP_EXTEND_MIN_SIZE (128UL * 1024UL)

#define SMALL_BIN_STRIDE (2UL * sizeof(size_t))

#define CURR_IN_USE_BIT ((size_t)0x1)
#define PREV_IN_USE_BIT ((size_t)0x2)
#define BITS ((size_t)0xF)

struct chunk {
    size_t prev_size;
    size_t size;
    struct chunk* fwd_ptr;
    struct chunk* bck_ptr;
};

#define CHUNK_HEADER_SIZE (2UL * sizeof(size_t))
#define MIN_CHUNK_SIZE (SMALL_BIN_STRIDE + CHUNK_HEADER_SIZE)

#define CEIL_MASK (SMALL_BIN_STRIDE - 1)

#define FAST_BIN_NUM 10
#define FAST_BIN_MAX_SIZE (MIN_CHUNK_SIZE + (FAST_BIN_NUM - 1) * SMALL_BIN_STRIDE)

#define SMALL_BIN_NUM 62
#define SMALL_BIN_MAX_SIZE (MIN_CHUNK_SIZE + (SMALL_BIN_NUM  - 1) * SMALL_BIN_STRIDE)

#define LARGE_BIN_NUM 63

static struct chunk* fast_bins[FAST_BIN_NUM];
static struct chunk small_bins[SMALL_BIN_NUM];
static struct chunk large_bins[LARGE_BIN_NUM];
static struct chunk unsorted_bin;

static bool is_initialized = false;

static struct chunk* heap_top_chunk;

bool is_curr_in_use(const struct chunk* chunk) {
    return (chunk->size & CURR_IN_USE_BIT) != 0;
}

bool is_prev_in_use(const struct chunk* chunk) {
    return (chunk->size & PREV_IN_USE_BIT) != 0;
}

size_t ceil_to_small_bin_size(size_t bytes) {
    return (bytes + CEIL_MASK) & ~CEIL_MASK;
}

size_t calculate_bytes(size_t size) {
    size_t result = ceil_to_small_bin_size(size + CHUNK_HEADER_SIZE);
    if (result < MIN_CHUNK_SIZE) {
        result = MIN_CHUNK_SIZE;
    }
    return result;
}

void* get_payload_ptr(struct chunk* chunk) {
    return (void*) ((char*)chunk + CHUNK_HEADER_SIZE);
}

int get_fast_bin_idx(size_t size) {
    if (size > FAST_BIN_MAX_SIZE) {
        return -1;
    }

    return (int) ((size - MIN_CHUNK_SIZE) / SMALL_BIN_STRIDE);
}

int get_small_bin_idx(size_t size) {
    if (size > SMALL_BIN_MAX_SIZE) {
        return -1;
    }

    return (int) ((size - MIN_CHUNK_SIZE) / SMALL_BIN_STRIDE);
}

int get_large_bin_idx(size_t size) {
    if (size <= SMALL_BIN_MAX_SIZE) {
        return 0;
    }

    size_t idx = (size - SMALL_BIN_MAX_SIZE - 1) / 512;
    if (idx >= LARGE_BIN_NUM) {
        idx = LARGE_BIN_NUM - 1;
    }
    
    return (int) idx;
}

bool is_empty(const struct chunk* head) {
    return head->fwd_ptr == head;
}

void init_chunk(struct chunk* chunk_ptr) {
    chunk_ptr->fwd_ptr = chunk_ptr;
    chunk_ptr->bck_ptr = chunk_ptr;
}

void remove_chunk(struct chunk* chunk) {
    chunk->bck_ptr->fwd_ptr = chunk->fwd_ptr;
    chunk->fwd_ptr->bck_ptr = chunk->bck_ptr;
    chunk->bck_ptr = NULL;
    chunk->fwd_ptr = NULL;
}

void insert_chunk(struct chunk* head, struct chunk* chunk) {
    chunk->fwd_ptr = head->fwd_ptr;
    chunk->bck_ptr = head;
    head->fwd_ptr->bck_ptr = chunk;
    head->fwd_ptr = chunk;
}

size_t get_chunk_size(struct chunk* chunk) {
    return chunk->size & ~BITS;
}

struct chunk* chunk_at_offset(struct chunk* chunk, long offset) {
    return (struct chunk*) ((char*)chunk + offset);
}

struct chunk* find_large_bin(size_t size) {
    int idx = get_large_bin_idx(size);
    for (int i = idx; i < LARGE_BIN_NUM; ++i) {
        struct chunk* head = &large_bins[i];
        for (struct chunk* curr = head->fwd_ptr; curr != head; curr = curr->fwd_ptr) {
            if (get_chunk_size(curr) >= size) {
                remove_chunk(curr);
                return curr;
            }
        }
    }
    return NULL;
}

void merge_with_neighbors(struct chunk* chunk) {
    size_t size = get_chunk_size(chunk);
    chunk->size &= ~CURR_IN_USE_BIT;

    if (!is_prev_in_use(chunk)) {
        struct chunk* prev = chunk_at_offset(chunk, -(long)chunk->prev_size);
        if (prev != heap_top_chunk) {
            remove_chunk(prev);
        }
        size += get_chunk_size(prev);
        chunk = prev;
    }

    struct chunk* next = chunk_at_offset(chunk, (long)size);
    if (next == heap_top_chunk) {
        size += get_chunk_size(heap_top_chunk);
        heap_top_chunk = chunk;
        heap_top_chunk->size = size | (heap_top_chunk->size & PREV_IN_USE_BIT);
        return;
    }

    if (!is_curr_in_use(next)) {
        remove_chunk(next);
        size += get_chunk_size(next);
    } else {
        next->prev_size = size;
        next->size &= ~PREV_IN_USE_BIT;
    }

    chunk->size = size | (chunk->size & PREV_IN_USE_BIT);

    next = chunk_at_offset(chunk, (long)size);
    next->prev_size = size;
    next->size &= ~PREV_IN_USE_BIT;

    insert_chunk(&unsorted_bin, chunk);
}

void flush_fast_bins() {
    for (int i = 0; i < FAST_BIN_NUM; ++i) {
        struct chunk* fast_bin = fast_bins[i];
        fast_bins[i] = NULL;
        while (fast_bin != NULL) {
            struct chunk* next = fast_bin->fwd_ptr;
            fast_bin->fwd_ptr = NULL;
            fast_bin->bck_ptr = NULL;
            fast_bin->size &= ~CURR_IN_USE_BIT;
            size_t chunk_size = get_chunk_size(fast_bin);

            struct chunk* next_continious_chunk = chunk_at_offset(fast_bin, (long) chunk_size);
            next_continious_chunk->prev_size = chunk_size;
            next_continious_chunk->size &= ~PREV_IN_USE_BIT;

            merge_with_neighbors(fast_bin);
            fast_bin = next;
        }
    }
}

struct chunk* update_neighbor(struct chunk* chunk, size_t size) {
    size_t chunk_size = get_chunk_size(chunk);
    size_t prev_bit_save = chunk->size & PREV_IN_USE_BIT;

    if (chunk_size >= size + MIN_CHUNK_SIZE) {
        size_t remaining = chunk_size - size;
        struct chunk* rem_chunk = chunk_at_offset(chunk, (long) size);
        rem_chunk->prev_size = size;
        rem_chunk->size = remaining | PREV_IN_USE_BIT;
        rem_chunk->fwd_ptr = NULL;
        rem_chunk->bck_ptr = NULL;

        struct chunk* rem_chunk_next = chunk_at_offset(rem_chunk, (long)remaining);
        rem_chunk_next->prev_size = remaining;
        rem_chunk_next->size &= ~PREV_IN_USE_BIT;

        insert_chunk(&unsorted_bin, rem_chunk);
        chunk->size = size | prev_bit_save | CURR_IN_USE_BIT;
        return chunk;
    }

    chunk->size = chunk_size | prev_bit_save | CURR_IN_USE_BIT;
    struct chunk* next = chunk_at_offset(chunk, (long) chunk_size);
    next->prev_size = chunk_size;
    next->size |= PREV_IN_USE_BIT;
    
    return chunk;
}

void init() {
    if (is_initialized) {
        return;
    }

    init_chunk(&unsorted_bin);

    for (int i = 0; i < SMALL_BIN_NUM; ++i) {
        init_chunk(&small_bins[i]);
    }

    for (int i = 0; i < LARGE_BIN_NUM; ++i) {
        init_chunk(&large_bins[i]);
    }

    void* mem_pool = sbrk((intptr_t)MMAP_THRESHOLD);
    if (mem_pool == (void*)-1) {
        return;
    }

    heap_top_chunk = (struct chunk*) mem_pool;
    heap_top_chunk->prev_size = 0;
    heap_top_chunk->size = MMAP_THRESHOLD | PREV_IN_USE_BIT;
    heap_top_chunk->bck_ptr = NULL;
    heap_top_chunk->fwd_ptr = NULL;

    is_initialized = true;
}

bool extend_top_chunk(size_t size) {
    if (heap_top_chunk == NULL) {
        return false;
    }

    if (size < HEAP_EXTEND_MIN_SIZE) {
        size = HEAP_EXTEND_MIN_SIZE;
    }
    size = ceil_to_small_bin_size(size);

    void* res = sbrk((intptr_t) size);
    if (res == (void*)-1) {
        return false;
    }

    heap_top_chunk->size = (get_chunk_size(heap_top_chunk) + size) | (heap_top_chunk->size & PREV_IN_USE_BIT);

    return true;
}

struct chunk* alloc_new_chunk(size_t size) {
    if (heap_top_chunk == NULL) {
        return NULL;
    }

    size_t heap_top_size = get_chunk_size(heap_top_chunk);
    if (heap_top_size < size) {
        if (!extend_top_chunk(size - heap_top_size)) {
            return NULL;
        }
        heap_top_size = get_chunk_size(heap_top_chunk);
    }

    struct chunk* res = heap_top_chunk;
    size_t prev_bit_save = res->size & PREV_IN_USE_BIT;
    
    if (heap_top_size >= size + MIN_CHUNK_SIZE) {
        struct chunk* new_heap_top = chunk_at_offset(res, (long) size);
        size_t new_heap_top_size = heap_top_size - size;

        new_heap_top->prev_size = size;
        new_heap_top->size = new_heap_top_size | PREV_IN_USE_BIT;
        new_heap_top->fwd_ptr = NULL;
        new_heap_top->bck_ptr = NULL;
        heap_top_chunk = new_heap_top;

        res->size = size | prev_bit_save | CURR_IN_USE_BIT;
        return res;
    }

    if (!extend_top_chunk(size + MIN_CHUNK_SIZE - heap_top_size)) {
        return NULL;
    }
    return alloc_new_chunk(size);
}

void *my_malloc(size_t size) {
    init();

    if (!is_initialized) {
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    size_t bytes = calculate_bytes(size);

    if (bytes >= MMAP_THRESHOLD) {
        void *result = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (result == MAP_FAILED) {
            return NULL;
        }

        struct chunk* hdr = (struct chunk*) result;
        hdr->prev_size = 0;
        hdr->size = bytes | PREV_IN_USE_BIT | CURR_IN_USE_BIT;
        hdr->fwd_ptr = NULL;
        hdr->bck_ptr = NULL;

        return get_payload_ptr(hdr);
    }

    int fast_bin_idx = get_fast_bin_idx(bytes);
    if (fast_bin_idx != -1) {
        struct chunk* fast_bin = fast_bins[fast_bin_idx];
        if (fast_bin != NULL) {
            fast_bins[fast_bin_idx] = fast_bin->fwd_ptr;
            fast_bin->fwd_ptr = NULL;
            fast_bin->bck_ptr = NULL;
            fast_bin->size |= CURR_IN_USE_BIT;
            return get_payload_ptr(fast_bin);
        }
    }

    int small_bin_idx = get_small_bin_idx(bytes);
    if (small_bin_idx != -1 && !is_empty(&small_bins[small_bin_idx])) {
        struct chunk* small_bin = small_bins[small_bin_idx].fwd_ptr;
        remove_chunk(small_bin);
        small_bin = update_neighbor(small_bin, bytes);
        return get_payload_ptr(small_bin);
    }

    flush_fast_bins();

    struct chunk* u_chunk = unsorted_bin.fwd_ptr;
    while (u_chunk != &unsorted_bin) {
        struct chunk* next = u_chunk->fwd_ptr;
        remove_chunk(u_chunk);
        if (get_chunk_size(u_chunk) >= bytes) {
            u_chunk = update_neighbor(u_chunk, bytes);
            return get_payload_ptr(u_chunk);
        }

        size_t u_size = get_chunk_size(u_chunk);
        u_chunk->size &= ~CURR_IN_USE_BIT;

        int small_bin_idx = get_small_bin_idx(u_size);
        if (small_bin_idx != -1) {
            insert_chunk(&small_bins[small_bin_idx], u_chunk);
        } else {
            int large_bin_idx = get_large_bin_idx(u_size);
            insert_chunk(&large_bins[large_bin_idx], u_chunk);
        }

        u_chunk = next;
    }

    if (small_bin_idx != -1 && !is_empty(&small_bins[small_bin_idx])) {
        struct chunk* small_bin = small_bins[small_bin_idx].fwd_ptr;
        remove_chunk(small_bin);
        small_bin = update_neighbor(small_bin, bytes);
        return get_payload_ptr(small_bin);
    }

    struct chunk* large_bin = find_large_bin(bytes);
    if (large_bin != NULL) {
        large_bin = update_neighbor(large_bin, bytes);
        return get_payload_ptr(large_bin);
    }

    struct chunk* new_chunk = alloc_new_chunk(bytes);
    if (new_chunk == NULL) {
        return NULL;
    }
    return get_payload_ptr(new_chunk);
}



void my_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct chunk* chunk = (struct chunk*) ((char*) ptr - CHUNK_HEADER_SIZE);
    size_t chunk_size = get_chunk_size(chunk);
    if (chunk_size >= MMAP_THRESHOLD) {
        munmap((void*) chunk, chunk_size);
        return;
    }
    
    int fast_bin_idx = get_fast_bin_idx(chunk_size);
    if (fast_bin_idx != -1) {
        chunk->fwd_ptr = fast_bins[fast_bin_idx];
        chunk->bck_ptr = NULL;
        chunk->size |= CURR_IN_USE_BIT;
        fast_bins[fast_bin_idx] = chunk;
        return;
    }

    chunk->size &= ~CURR_IN_USE_BIT;
    merge_with_neighbors(chunk);
}

void *my_realloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return my_malloc(size);
    }

    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    struct chunk* old_chunk = (struct chunk*) ((char*)ptr - CHUNK_HEADER_SIZE);
    size_t old_chunk_size = get_chunk_size(old_chunk);
    size_t old_payload_size = old_chunk_size - CHUNK_HEADER_SIZE;
    if (size <= old_payload_size) {
        return ptr;
    }

    void* new_ptr = my_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    size_t copy_size = old_payload_size < size ? old_payload_size : size;
    memcpy(new_ptr, ptr, copy_size);

    my_free(ptr);
    return new_ptr;
}
