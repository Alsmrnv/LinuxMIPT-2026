#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/mm.h> 
#include <linux/pgtable.h>

SYSCALL_DEFINE1(get_addr, unsigned long, v_addr)
{
    struct mm_struct* mm = current->mm;
    if (!mm) {
        // TODO
    }

    pgd_t* pgd;
    p4d_t* p4d;
    pud_t* pud; 
    pmd_t* pmd;
    pte_t* pte;


    pgd = pgd_offset(mm, v_addr);
    p4d = p4d_offset(pgd, v_addr);
    pud = pud_offset(p4d, v_addr);
    pmd = pmd_offset(pud, v_addr);

    pte = pte_offset_map(pmd, v_addr);

    struct page* page = pte_page(*pte);
    unsigned long offset = v_addr & ~PAGE_MASK;
    unsigned long p_addr = page_to_phys(page) + offset;

    pte_unmap(pte);
    
    return p_addr;
}

