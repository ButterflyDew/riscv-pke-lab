#include "kernel/process.h"
#include "kernel/riscv.h"
#include "/Users/butterflydew/Downloads/OS_core/riscv-pke/kernel/process.h"
#include "/Users/butterflydew/Downloads/OS_core/riscv-pke/spike_interface/spike_file.h"
#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "util/string.h"

static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() { panic("Illegal instruction!"); }

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}
char path[1<<12],inst[1<<12];
void print_error()
{
  int line_num = -1;
  for(int i = 0; i < current->line_ind; i++)
    if(current->line[i].addr == read_csr(mepc))
    {
      strcpy(path, current->dir[current->line[i].file]);
      int len = strlen(path);
      path[len] = '/';
      strcpy(path + len + 1,current->file[current->line[i].file].file);
      sprint("Runtime error at %s:%d\n", path, line_num = current->line[i].line);
    }
  spike_file_t *file = spike_file_open(path, O_RDONLY, 0);
  int p = 0, off = 0, line = 0;
  char now;
  while(233)
  {
    spike_file_pread(file, (char *)&now, 1, off);
    ++off;
    if(line == line_num - 1)
    {
      if(now == '\n')
      {
        inst[p++] = '\0';
        break;
      }
      inst[p++] = now;
    }  
    if(now == '\n') ++line;
  }
  sprint("%s\n",inst);
}
//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  switch (mcause) {
    case CAUSE_MTIMER:
      print_error();
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      print_error();
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      print_error();
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      print_error();
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
     // panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );
      print_error();
      handle_illegal_instruction();
      break;
    case CAUSE_MISALIGNED_LOAD:
      print_error();
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      print_error();
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}
