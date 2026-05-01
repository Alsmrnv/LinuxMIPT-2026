#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(void) {
    int a = 5;
    unsigned long v_addr = (unsigned long)&a;
    unsigned long p_addr = syscall(777, v_addr);

    printf("a virtual address: %lx \na physical address: %lx \n", v_addr, p_addr);

    int* ptr = (int*) malloc(sizeof(int));
    *ptr = 5;
    v_addr = (unsigned long)ptr;
    p_addr = syscall(777, v_addr);
    printf("ptr virtual address: %lx \nptr physical address: %lx \n", v_addr, p_addr);
    return 0;
}

