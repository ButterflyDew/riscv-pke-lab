/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "elf.h"
#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

ssize_t sys_user_print_backtrace(uint64 dep)
{
  // sprint("sp: %p,fp: %p\n",(uint64 *)current->trapframe->regs.sp,(uint64 *)current->trapframe->regs.s0);
  // for(int i=10;i>=-5;i--) 
  // {
  //   sprint("fp: %p,*fp: %p\n",(uint64 *)(current->trapframe->regs.s0)+i,
  //   *((uint64 *)(current->trapframe->regs.s0)+i));
  // }
  // uint64 fp = current->trapframe->regs.s0;
  // for(int i=0;i<dep;i++)
  // {
  //   fp = *((uint64 *)fp);
  //   sprint("fp : %p\n",fp);
  // }

    uint64 fp = *((uint64*)(current->trapframe->regs.s0-8));

  // uint64 fp2 = *((uint64*)current->trapframe->regs.s0 - 1);
  // sprint("fp : %p fp2: %p\n",fp,fp2);
    int length;
    char namelist[1024];
    elf_symbol sym[256];
    load_elf_name(current, &length, namelist, sym);
    sprint("back trace the user app in the following:\n");
    for (int i=0;i<dep;i++) {
      //sprint("fp: %p\n", fp);
      uint64 ra = *((uint64*)fp - 1);
      if (!ra) break;
      //sprint("len: %p\n",length);
      int indx = -1;
      uint64 mx = 0;
      //sprint("ra: %p\n", ra);
      for(int j=0;j<length;j++)
      {
        //sprint("info:%p val:%p name:%s\n",sym[j].st_info,sym[j].st_value,
        //&namelist[sym[j].st_name]);
        if(sym[j].st_info == 18 && sym[j].st_value < ra && sym[j].st_value > mx)
        {
          mx = sym[j].st_value;
          indx = j;
        }
      }
      //sprint("indx:%p\n",indx);
      sprint("%s\n",&namelist[sym[indx].st_name]);
      
      //sprint("fp/*: %p\n",(uint64)((uint64*)(fp - 16)));
      fp = *((uint64*)(fp - 16));
    }
    return 0;
}
//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_print_backtrace:
      return sys_user_print_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
