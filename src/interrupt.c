#include "config.h"
#include "cpu_info.h"
#include "interrupt.h"
#include "memory.h"
#include "sys_table.h"

static uint8_t (*isr_stacks)[CONFIG_ISR_STACK_SIZE];
static struct sys_task_segment *task_segments;
static struct sys_idt_desc idt[INT_VECTORS];

static void create_gdt(void) {
  int cpus = get_cpus();
  isr_stacks = kmalloc(cpus * CONFIG_ISR_STACK_SIZE);
  task_segments = kmalloc(cpus * sizeof(struct sys_task_segment));
  if (!isr_stacks || !task_segments) {
    LOG_ERROR("failed to allocate memory");
    ASMV("jmp halt");
  }
  memset(task_segments, 0, cpus * sizeof(struct sys_task_segment));

  extern uint8_t gdt[];
  struct sys_gdt_desc2 *gdt2 =
    (struct sys_gdt_desc2*)(gdt + 3 * SYS_GDT_DESC_SIZE);

  for (int i = 0; i < cpus; i++) {
    task_segments[i].ists[0] = (uint64_t)&isr_stacks[i + 1];
    uint64_t tss_addr = (uint64_t)&task_segments[i];

    gdt2[i] = (struct sys_gdt_desc2) { {
        .type = SYS_SEGMENT_TSS, .nonsys = false, .present = true,
        .base0 = (uint16_t)tss_addr, .base1 = (uint8_t)(tss_addr >> 16),
        .limit0 = sizeof(struct sys_task_segment) - 1,
        .base2 = (uint8_t)(tss_addr >> 24)
      },
      .base3 = (uint32_t)(tss_addr >> 32)
    };
  }
}

void dump_int_stack_frame(const struct int_stack_frame *stack_frame) {
  kprintf("rax: %lX\n", stack_frame->rax);
  kprintf("rbx: %lX\n", stack_frame->rbx);
  kprintf("rcx: %lX\n", stack_frame->rcx);
  kprintf("rdx: %lX\n", stack_frame->rdx);
  kprintf("rsi: %lX\n", stack_frame->rsi);
  kprintf("rdi: %lX\n", stack_frame->rdi);
  kprintf("r8: %lX\n", stack_frame->r8);
  kprintf("r9: %lX\n", stack_frame->r9);
  kprintf("r10: %lX\n", stack_frame->r10);
  kprintf("r11: %lX\n", stack_frame->r11);
  kprintf("r12: %lX\n", stack_frame->r12);
  kprintf("r13: %lX\n", stack_frame->r13);
  kprintf("r14: %lX\n", stack_frame->r14);
  kprintf("r15: %lX\n", stack_frame->r15);
  kprintf("rip: %lX\n", stack_frame->rip);
  kprintf("rsp: %lX\n", stack_frame->rsp);
  kprintf("cs: %X\n", stack_frame->cs);
  kprintf("ss: %X\n", stack_frame->ss);
  kprintf("rflags: %lX\n", stack_frame->rflags);
}

DEFINE_INT_HANDLER(default) {
  kprintf("\nfault: #%s", (char*)(data << 8 >> 8));
  if (is_int_error(data >> 56))
    kprintf(" (error_code: %X)", stack_frame->error_code);
  kprintf("\n");
  dump_int_stack_frame(stack_frame);
  kprintf("\n\n\n\n");
  ASMV("jmp halt");
}

#define DEFINE_DEFAULT_ISR_WRAPPER(mnemonic)                            \
  DEFINE_ISR_WRAPPER(mnemonic, default,                                 \
                     ((uint64_t)INT_VECTOR_##mnemonic << 56) + #mnemonic)

DEFINE_DEFAULT_ISR_WRAPPER(DE);
DEFINE_DEFAULT_ISR_WRAPPER(NMI);
DEFINE_DEFAULT_ISR_WRAPPER(BP);
DEFINE_DEFAULT_ISR_WRAPPER(OF);
DEFINE_DEFAULT_ISR_WRAPPER(BR);
DEFINE_DEFAULT_ISR_WRAPPER(UD);
DEFINE_DEFAULT_ISR_WRAPPER(NM);
DEFINE_DEFAULT_ISR_WRAPPER(DF)
DEFINE_DEFAULT_ISR_WRAPPER(TS);
DEFINE_DEFAULT_ISR_WRAPPER(NP);
DEFINE_DEFAULT_ISR_WRAPPER(SS);
DEFINE_DEFAULT_ISR_WRAPPER(GP);
DEFINE_DEFAULT_ISR_WRAPPER(PF);
DEFINE_DEFAULT_ISR_WRAPPER(MF);
DEFINE_DEFAULT_ISR_WRAPPER(AC);
DEFINE_DEFAULT_ISR_WRAPPER(MC);
DEFINE_DEFAULT_ISR_WRAPPER(XM);

#define CODE_SEGMENT 8

static void create_idt(void) {
  for (int i = 0; i < INT_VECTORS; i++)
    if (!is_int_reserved(i))
      idt[i] = (struct sys_idt_desc) {
        .cs = CODE_SEGMENT, .ist = 1, .type = SYS_GATE_INT, .present = false
      };

  set_isr(INT_VECTOR_DE, get_DE_isr());
  set_isr(INT_VECTOR_NMI, get_NMI_isr());
  set_isr(INT_VECTOR_BP, get_BP_isr());
  set_isr(INT_VECTOR_OF, get_OF_isr());
  set_isr(INT_VECTOR_BR, get_BR_isr());
  set_isr(INT_VECTOR_UD, get_UD_isr());
  set_isr(INT_VECTOR_NM, get_NM_isr());
  set_isr(INT_VECTOR_DF, get_DF_isr());
  set_isr(INT_VECTOR_TS, get_TS_isr());
  set_isr(INT_VECTOR_NP, get_NP_isr());
  set_isr(INT_VECTOR_SS, get_SS_isr());
  set_isr(INT_VECTOR_GP, get_GP_isr());
  set_isr(INT_VECTOR_PF, get_PF_isr());
  set_isr(INT_VECTOR_MF, get_MF_isr());
  set_isr(INT_VECTOR_AC, get_AC_isr());
  set_isr(INT_VECTOR_MC, get_MC_isr());
  set_isr(INT_VECTOR_XM, get_XM_isr());
}

static void load_gdt_idt_tr(void) {
  extern uint8_t gdt[];
  struct sys_table_info gdti = {
    (3 * SYS_GDT_DESC_SIZE) + (get_cpus() * SYS_GDT_DESC2_SIZE) - 1,
    (uint64_t)gdt
  };
  struct sys_table_info idti = {
    SYS_IDT_DESC_SIZE * INT_VECTORS - 1, (uint64_t)idt
  };
  uint16_t sel = SYS_GDT_DESC_SIZE * 3 + SYS_GDT_DESC2_SIZE * get_cpu();
  ASMV("lgdt %0\nlidt %1\nmovw %2, %%ax\nltr %%ax"
       : : "m"(gdti), "m"(idti), "m"(sel) : "ax");
}

void init_interrupts(void) {
  int cpu = get_cpu();
  if (cpu == get_bsp_cpu()) {
    create_gdt();
    create_idt();
  }
  load_gdt_idt_tr();
  LOG_DEBUG("done (CPU: %d)", cpu);
}

void *get_isr(int vector) {
  struct sys_idt_desc *desc = idt + vector;
  return (void*)(desc->handler0 + ((uint64_t)desc->handler1 << 16) +
                 ((uint64_t)desc->handler2 << 32));
}

void set_isr(int vector, void *isr) {
  struct sys_idt_desc *desc = idt + vector;
  uint64_t isri = (uint64_t)isr;
  desc->present = isri != 0;
  desc->handler0 = (uint16_t)isri;
  desc->handler1 = (uint16_t)(isri >> 16);
  desc->handler2 = (uint32_t)(isri >> 32);
}
