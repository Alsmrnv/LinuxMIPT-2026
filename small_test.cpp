#include <iostream>
#include <seccomp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <cstdlib>
#include <cstddef>
#include <cassert>

extern "C" {
void* my_malloc(size_t size);
void my_free(void* ptr);
void* my_realloc(void* ptr, size_t size);
}

void setup_seccomp() {
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);

    if (ctx == nullptr) {
        perror("seccomp_init");
        exit(1);
    }

    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0);

    if (seccomp_load(ctx) < 0) {
        perror("seccomp_load");
        exit(1);
    }

    seccomp_release(ctx);
}

int main() {
    
    std::cout << "Starting malloc." << std::endl;
    
    setup_seccomp();

    // 1. We check that for small allocations, only brk() syscall is used.
    void* current_brk = sbrk(0);
    void* ptrs[1000];
    for (int i = 1; i < 1000; ++i) {
        ptrs[i] = my_malloc(i);
    }
       
    // 2. Test that we did not asked too much from OS, i.e. brk has been moved not too far
    //   We have allocated totally 1000*1001/2 = 500'500 bytes,
    //   and we assume that brk has moved not too further than this number
    assert(((char*)sbrk(0) - (char*)current_brk < 550'000));

    // 3. Test that memory is indeed allocated, i.e. we can write into it
    for (int i = 1; i < 1000; ++i) {
        for (int j = 0; j < i; ++j) {
            *((char*) ptrs[i] + j) = 'a';
        }
    }

    // 4. Test that malloc always returns aligned memory
    for (int i = 1; i < 1000; ++i) {
        assert(((uint64_t) ptrs[i] % alignof(std::max_align_t) == 0));
    }

    // 5. Check that memory is reused, i.e. repeated allocations of the same sizes
    //  will not call brk() again
    current_brk = sbrk(0);
    for (int i = 1; i < 1000; ++i) {
        my_free(ptrs[i]);
    }
    for (int i = 1000; i >= 1; --i) {
        ptrs[i] = my_malloc(i);
    }
    assert(current_brk == sbrk(0));

    // 6. Check that if I my_free() and then immediately my_malloc() the same size,
    //  I get the same address
    for (int i = 1; i < 500; ++i) {
        void* oldptr = ptrs[i];
        my_free(ptrs[i]);
        ptrs[i] = my_malloc(i);
        // std::cout << i << std::endl;
        assert(oldptr == ptrs[i]);
    }

    // 7. Check sizes of bins.
    // Bin size should be stored just before the pointer returned by malloc.
    //
    for (int i = 1; i < 500; ++i) {
        size_t bin_size = *(size_t*)((char*)ptrs[i] - sizeof(size_t));
        
        // Bin sizes should be multiples of 16.
        // Low 4 bits are flags in our allocator.
        assert(bin_size & 0x1);
        bin_size &= ~0xFULL;
        assert(bin_size % 16 == 0);

        // Bin sizes should not be too big.
        assert(bin_size > i);
        // std::cout << i << ' ' << bin_size << std::endl;
        assert(i < 32 || bin_size - i <= 32);
    }

    // 8. Check that memory from smaller bins can be reused for bigger sizes.
    current_brk = sbrk(0);
    
    for (int i = 1; i < 500; ++i) {
        my_free(ptrs[i]);
    }
    
    // We freed 500*501/2 = 125'250 bytes,
    // it should be enough for allocating 150*700 bytes more without calling brk()
    void* newptrs[150];
    for (int i = 0; i < 150; ++i) {
        newptrs[i] = my_malloc(700);
    }

    assert(current_brk == sbrk(0));
    
    for (int i = 0; i < 150; ++i) {
        my_free(newptrs[i]);
    }

    // 9. And now check that this memory again can be reused for small bins
    for (int i = 1; i < 500; ++i) {
        ptrs[i] = my_malloc(i);
    }
    assert(current_brk == sbrk(0));

    // 10. Test realloc. First, check that if we extend memory not exceeding bins size,
    //   then realloc will not move it to anywhere
    for (int i = 1; i < 1000; ++i) {
        void* oldptr = ptrs[i];
        size_t increase = i % 8 ? 8 - i % 8 : 0;
        size_t newsize = i + increase;
        ptrs[i] = my_realloc(ptrs[i], newsize);
        assert(oldptr == ptrs[i]);
    }
    for (int i = 1; i < 1000; ++i) {
        my_free(ptrs[i]);
    }

    // 11. Check that realloc is indeed able to reallocate
    {
        void* ptr = my_malloc(50);
        size_t bin_size = *(size_t*)((char*)ptr - sizeof(size_t));
        // std::cout << bin_size << std::endl;
        assert(bin_size & 0x1);
        bin_size = bin_size & ~0xFULL;
        assert(bin_size <= 80);
        for (int i = 0; i < 50; ++i) {
            *((char*)ptr + i) = 'a';
        }
        // std::cout << bin_size << std::endl;
        
        void* newptr = my_realloc(ptr, 120);
        size_t new_bin_size = *(size_t*)((char*)newptr - sizeof(size_t));
        assert(new_bin_size & 0x1);
        new_bin_size = new_bin_size & ~0xFULL;
        assert(new_bin_size > 80);
        assert(new_bin_size < 150);

        for (int i = 0; i < 50; ++i) {
            char c = *((char*)newptr + i);
            assert(c == 'a');
        }
        
        // For some reason, for std::realloc this doesn't hold, so we skip this check
        /*if (ptr != newptr) {
            size_t bin_size = *(size_t*)((char*)ptr - sizeof(size_t));
            assert(bin_size % 16 == 0);
        }*/

        my_free(newptr);
    }
}

