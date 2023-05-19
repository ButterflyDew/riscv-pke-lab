/*
 * virtual address mapping related functions.
 */

#include "vmm.h"
#include "/Users/butterflydew/Downloads/OS_core/riscv-pke/kernel/process.h"
#include "riscv.h"
#include "pmm.h"
#include "util/types.h"
#include "memlayout.h"
#include "util/string.h"
#include "spike_interface/spike_utils.h"
#include "util/functions.h"

/* --- utility functions for virtual address mapping --- */
//
// establish mapping of virtual address [va, va+size] to phyiscal address [pa, pa+size]
// with the permission of "perm".
//
int map_pages(pagetable_t page_dir, uint64 va, uint64 size, uint64 pa, int perm) {
  uint64 first, last;
  pte_t *pte;

  for (first = ROUNDDOWN(va, PGSIZE), last = ROUNDDOWN(va + size - 1, PGSIZE);
      first <= last; first += PGSIZE, pa += PGSIZE) {
    if ((pte = page_walk(page_dir, first, 1)) == 0) return -1;
    if (*pte & PTE_V)
      panic("map_pages fails on mapping va (0x%lx) to pa (0x%lx)", first, pa);
    *pte = PA2PTE(pa) | perm | PTE_V;
  }
  return 0;
}

//
// convert permission code to permission types of PTE
//
uint64 prot_to_type(int prot, int user) {
  uint64 perm = 0;
  if (prot & PROT_READ) perm |= PTE_R | PTE_A;
  if (prot & PROT_WRITE) perm |= PTE_W | PTE_D;
  if (prot & PROT_EXEC) perm |= PTE_X | PTE_A;
  if (perm == 0) perm = PTE_R;
  if (user) perm |= PTE_U;
  return perm;
}

//
// traverse the page table (starting from page_dir) to find the corresponding pte of va.
// returns: PTE (page table entry) pointing to va.
//
pte_t *page_walk(pagetable_t page_dir, uint64 va, int alloc) {
  if (va >= MAXVA) panic("page_walk");

  // starting from the page directory
  pagetable_t pt = page_dir;

  // traverse from page directory to page table.
  // as we use risc-v sv39 paging scheme, there will be 3 layers: page dir,
  // page medium dir, and page table.
  for (int level = 2; level > 0; level--) {
    // macro "PX" gets the PTE index in page table of current level
    // "pte" points to the entry of current level
    pte_t *pte = pt + PX(level, va);

    // now, we need to know if above pte is valid (established mapping to a phyiscal page)
    // or not.
    if (*pte & PTE_V) {  //PTE valid
      // phisical address of pagetable of next level
      pt = (pagetable_t)PTE2PA(*pte);
    } else { //PTE invalid (not exist).
      // allocate a page (to be the new pagetable), if alloc == 1
      if( alloc && ((pt = (pte_t *)alloc_page(1)) != 0) ){
        memset(pt, 0, PGSIZE);
        // writes the physical address of newly allocated page to pte, to establish the
        // page table tree.
        *pte = PA2PTE(pt) | PTE_V;
      }else //returns NULL, if alloc == 0, or no more physical page remains
        return 0;
    }
  }

  // return a PTE which contains phisical address of a page
  return pt + PX(0, va);
}

//
// look up a virtual page address, return the physical page address or 0 if not mapped.
//
uint64 lookup_pa(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA) return 0;

  pte = page_walk(pagetable, va, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || ((*pte & PTE_R) == 0 && (*pte & PTE_W) == 0))
    return 0;
  pa = PTE2PA(*pte);

  return pa;
}

/* --- kernel page table part --- */
// _etext is defined in kernel.lds, it points to the address after text and rodata segments.
extern char _etext[];

// pointer to kernel page director
pagetable_t g_kernel_pagetable;

//
// maps virtual address [va, va+sz] to [pa, pa+sz] (for kernel).
//
void kern_vm_map(pagetable_t page_dir, uint64 va, uint64 pa, uint64 sz, int perm) {
  // map_pages is defined in kernel/vmm.c
  if (map_pages(page_dir, va, sz, pa, perm) != 0) panic("kern_vm_map");
}

//
// kern_vm_init() constructs the kernel page table.
//
void kern_vm_init(void) {
  // pagetable_t is defined in kernel/riscv.h. it's actually uint64*
  pagetable_t t_page_dir;

  // allocate a page (t_page_dir) to be the page directory for kernel. alloc_page is defined in kernel/pmm.c
  t_page_dir = (pagetable_t)alloc_page();
  // memset is defined in util/string.c
  memset(t_page_dir, 0, PGSIZE);

  // map virtual address [KERN_BASE, _etext] to physical address [DRAM_BASE, DRAM_BASE+(_etext - KERN_BASE)],
  // to maintain (direct) text section kernel address mapping.
  kern_vm_map(t_page_dir, KERN_BASE, DRAM_BASE, (uint64)_etext - KERN_BASE,
         prot_to_type(PROT_READ | PROT_EXEC, 0));

  sprint("KERN_BASE 0x%lx\n", lookup_pa(t_page_dir, KERN_BASE));

  // also (direct) map remaining address space, to make them accessable from kernel.
  // this is important when kernel needs to access the memory content of user's app
  // without copying pages between kernel and user spaces.
  kern_vm_map(t_page_dir, (uint64)_etext, (uint64)_etext, PHYS_TOP - (uint64)_etext,
         prot_to_type(PROT_READ | PROT_WRITE, 0));

  sprint("physical address of _etext is: 0x%lx\n", lookup_pa(t_page_dir, (uint64)_etext));

  g_kernel_pagetable = t_page_dir;
}

/* --- user page table part --- */
//
// convert and return the corresponding physical address of a virtual address (va) of
// application.
//
void *user_va_to_pa(pagetable_t page_dir, void *va) {
  // TODO (lab2_1): implement user_va_to_pa to convert a given user virtual address "va"
  // to its corresponding physical address, i.e., "pa". To do it, we need to walk
  // through the page table, starting from its directory "page_dir", to locate the PTE
  // that maps "va". If found, returns the "pa" by using:
  // pa = PYHS_ADDR(PTE) + (va & (1<<PGSHIFT -1))
  // Here, PYHS_ADDR() means retrieving the starting address (4KB aligned), and
  // (va & (1<<PGSHIFT -1)) means computing the offset of "va" inside its page.
  // Also, it is possible that "va" is not mapped at all. in such case, we can find
  // invalid PTE, and should return NULL.

  uint64 ptr = (uint64) va;
  pte_t *PTE = page_walk(page_dir, ptr, 0);
  if(PTE==NULL) return NULL;
  uint64 pos = PTE2PA(*PTE);
  uint64 pa = pos + (ptr & ((1<<PGSHIFT)-1));
  return (void *)pa;
  //panic( "You have to implement user_va_to_pa (convert user va to pa) to print messages in lab2_1.\n" );

}

//
// maps virtual address [va, va+sz] to [pa, pa+sz] (for user application).
//
void user_vm_map(pagetable_t page_dir, uint64 va, uint64 size, uint64 pa, int perm) {
  if (map_pages(page_dir, va, size, pa, perm) != 0) {
    panic("fail to user_vm_map .\n");
  }
}

//
// unmap virtual address [va, va+size] from the user app.
// reclaim the physical pages if free!=0
//
void user_vm_unmap(pagetable_t page_dir, uint64 va, uint64 size, int free) {
  // TODO (lab2_2): implement user_vm_unmap to disable the mapping of the virtual pages
  // in [va, va+size], and free the corresponding physical pages used by the virtual
  // addresses when if 'free' (the last parameter) is not zero.
  // basic idea here is to first locate the PTEs of the virtual pages, and then reclaim
  // (use free_page() defined in pmm.c) the physical pages. lastly, invalidate the PTEs.
  // as naive_free reclaims only one page at a time, you only need to consider one page
  // to make user/app_naive_malloc to behave correctly.
  //panic( "You have to implement user_vm_unmap to free pages using naive_free in lab2_2.\n" );
  if(free)
  {
    uint64 ptr = (uint64) va;
    pte_t *PTE = page_walk(page_dir, ptr, 0);
    if(PTE==NULL) return;
    uint64 pos = (PTE2PA(*PTE));
    free_page((void *)pos);
  }
}

MCB *head = NULL,*tail = NULL;
int typ;
uint64 alloc_n_size(uint64 n)
{
  MCB* cur = head;
  while(cur != NULL)
  {
    if(cur->used == 0 && cur -> size >= n + sizeof(MCB))
      return cur->v_addr;
    cur = cur->suc;
  }
  typ = 1;
  g_ufree_page = ((g_ufree_page + 15) >> 4)<<4;

  if(ROUNDDOWN(g_ufree_page, PGSIZE) != ROUNDDOWN(g_ufree_page + sizeof(MCB) -1, PGSIZE))
    g_ufree_page = ROUNDDOWN(g_ufree_page + sizeof(MCB) -1, PGSIZE);
  uint64 lpos = ROUNDDOWN(g_ufree_page, PGSIZE);
  uint64 rpos = ROUNDDOWN(g_ufree_page + n +sizeof(MCB) -1 , PGSIZE);
  // sprint("g_u_page: %p, n:%p\n",g_ufree_page,n);
  // sprint("[lpos,rpos]=[%p, %p]\n",lpos,rpos);
  uint64 now = lpos;
  while(now <= rpos)
  {
    pte_t *pte = page_walk(current->pagetable, now, 1);
    if((*pte & PTE_V) == 0)
    {
      uint64 npg = (uint64) alloc_page();
      * pte = PA2PTE(npg) | PTE_V | prot_to_type(PROT_WRITE | PROT_READ, 1);
      //sprint("pte:%p\n",*pte);
    }
    now += PGSIZE;
  }
  return g_ufree_page;
}
uint64 user_better_allocate(uint64 n)
{
  typ = 0;
  uint64 mcb_addr = alloc_n_size(n);
  MCB *now = (MCB *) user_va_to_pa(current->pagetable, (void *)mcb_addr);
  if(typ)
  {
    now->used = 1;
    now->suc = NULL;
    now->pre = NULL;
    now->size = n + sizeof(MCB);
    now->v_addr = mcb_addr;
    if(head == NULL) head = tail = now;
    else
    {
      tail -> suc = now;
      now -> pre = tail;
      tail = now;
    }
    g_ufree_page += n + sizeof(MCB);
    return mcb_addr + sizeof(MCB);
  }
  else 
  {
    //sprint("found res\n");
    now->used = 1;
    now->size = n + sizeof(MCB);
    if(now->suc!=NULL) 
    {
      uint64 nxtpos = now->v_addr + now->size;
      nxtpos = (nxtpos + 15) <<4 >> 4;
      if(nxtpos + sizeof(MCB) < now->suc->v_addr)
      {
        MCB *nxtmcb = (MCB *) user_va_to_pa(current->pagetable, (void *)nxtpos);
        nxtmcb->pre = now;
        nxtmcb->suc = now->suc;
        nxtmcb->size = now->suc->v_addr - nxtpos +1;
        nxtmcb->used = 0;
        nxtmcb->v_addr = nxtpos;
        now->suc->pre = nxtmcb;
        now->suc = nxtmcb;
      }
    }
    return now->v_addr + sizeof(MCB);
  }
}



uint64 user_better_free(uint64 va)
{
  //sprint("va:%p\n",va);
  MCB *now = (MCB *) user_va_to_pa(current->pagetable, (void *)(va - sizeof(MCB)));
  //sprint("now:%p\n",now);
  now->used = 0;
  MCB *lpos = now, *rpos = now;
  uint64 len = now->size;
  while(lpos->pre != NULL && lpos->pre->used == 0) lpos = lpos ->pre, len += lpos->size;
  while(rpos->suc != NULL && rpos->suc->used == 0) rpos = rpos ->suc, len += rpos->size;
  MCB *res = lpos;
  res -> used = 0;
  res -> suc = rpos -> suc;
  if(rpos->suc!=NULL) rpos->suc->pre = res;
  res -> size = len;
  return 0;
  // uint64 lpos = ROUNDDOWN(va - sizeof(MCB), PGSIZE);
  // uint64 rpos = ROUNDDOWN(va - sizeof(MCB) + now->size-1 , PGSIZE);
  // sprint("[lpos,rpos]=[%p, %p]\n",lpos,rpos);
  // uint64 nowpos = lpos + PGSIZE;
  // while(nowpos < rpos)
  // {
  //   pte_t *pte;
  //   pte = page_walk(current->pagetable, nowpos, 0);
  //   if(pte != 0 &&(*pte & PTE_V))
  //   {
  //     *pte ^= PTE_V;
  //     free_page((void *)(PTE2PA(*pte)));
  //   }
  //   nowpos += PGSIZE;
  // }
  // uint64 pre_addr = 0,suc_addr = 0;
  // if(now->pre!=NULL) pre_addr = ROUNDDOWN(now->pre->v_addr + now->pre->size -1,PGSIZE);
  // if(now->suc!=NULL) suc_addr = ROUNDDOWN(now->suc->v_addr, PGSIZE);

  // sprint("[pre_addr,suc_addr] = [%p, %p]\n",pre_addr,suc_addr);
  // if(lpos == rpos)
  // {
  //   if(lpos != pre_addr && rpos != suc_addr)
  //   {
  //     pte_t *pte;
  //     pte = page_walk(current->pagetable, lpos, 0);
  //     if(pte != 0 &&(*pte & PTE_V))
  //     {
  //       *pte ^= PTE_V;
  //       free_page((void *)(PTE2PA(*pte)));
  //     }
  //   }
  // }
  // else
  // {
  //   if(lpos != pre_addr)
  //   {
  //     pte_t *pte;
  //     pte = page_walk(current->pagetable, lpos, 0);
  //     if(pte != 0 &&(*pte & PTE_V))
  //     {
  //       *pte ^= PTE_V;
  //       free_page((void *)(PTE2PA(*pte)));
  //     }
  //   }
  //   if(rpos != suc_addr)
  //   {
  //     pte_t *pte;
  //     pte = page_walk(current->pagetable, rpos, 0);
  //     if(pte != 0 &&(*pte & PTE_V))
  //     {
  //       *pte ^= PTE_V;
  //       free_page((void *)(PTE2PA(*pte)));
  //     }
  //   }
  // }
  // sprint("free is done\n");
  // sprint("tail:%p now:%p\n",tail,now);
  // if(tail == now && head == now)
  // {
  //   head = tail = now = NULL;
  // }
  // if(tail == now)
  // {
  //   tail = now->pre;
  //   now->pre->suc = NULL;
  //   now = NULL;
  // }
  // else if(now==head)
  // {
  //   head = now->suc;
  //   now->suc->pre = NULL;
  //   now = NULL;
  // }
  // else
  // {
  //   now->pre->suc = now->suc;
  //   now->suc->pre = now->pre;
  //   now = NULL; 
  // }
  // return 0;
}