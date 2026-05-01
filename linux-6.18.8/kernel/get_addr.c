#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/mm.h> 
#include <linux/pgtable.h>

SYSCALL_DEFINE1(get_addr, unsigned long, v_addr)
{
    struct mm_struct* mm = current->mm;
    if (!mm) {
        return -ESRCH;
    }

    pgd_t* pgd;
    p4d_t* p4d;
    pud_t* pud; 
    pmd_t* pmd;
    pte_t* pte;

    pgd = pgd_offset(mm, v_addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        return -EINVAL;
    }

    p4d = p4d_offset(pgd, v_addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        return -EINVAL;
    }

    pud = pud_offset(p4d, v_addr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        return -EINVAL;
    }

    pmd = pmd_offset(pud, v_addr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        return -EINVAL;
    }

    pte = pte_offset_map(pmd, v_addr);
    if (!pte) {
        return -EINVAL;
    }

    if (!pte_present(*pte)) {
        pte_unmap(pte);
        return -EINVAL;
    }

    struct page* page = pte_page(*pte);
    unsigned long offset = v_addr & ~PAGE_MASK;
    unsigned long p_addr = page_to_phys(page) + offset;

    pte_unmap(pte);
    
    return p_addr;
}

