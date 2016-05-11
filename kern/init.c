/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/memlayout.h>

#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/kdebug.h>
#include <kern/dwarf_api.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/trap.h>
#include <kern/sched.h>
#include <kern/picirq.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>
#include <kern/time.h>
#include <kern/pci.h>
#if defined(TEST_EPT_MAP)
int test_ept_map(void);
#endif

uint64_t end_debug;

static void boot_aps(void);


void
i386_init(void)
{
    /* __asm __volatile("int $12"); */

	extern char edata[], end[];

	// Before doing anything else, complete the ELF loading process.
	// Clear the uninitialized global data (BSS) section of our program.
	// This ensures that all static/global variables start out zero.
	memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

	cprintf("6828 decimal is %o octal!\n", 6828);

#ifdef VMM_GUEST
	/* Guest VMX extension exposure check */
	{
		uint32_t ecx = 0;
		cpuid(0x1, NULL, NULL, &ecx, NULL);
		if (ecx & 0x20)
			panic("[ERR] VMX extension exposed to guest.\n");
		else
			cprintf("VMX extension hidden from guest.\n");
	}
#endif

#ifndef VMM_GUEST
    extern char end[];
    end_debug = read_section_headers((0x10000+KERNBASE), (uintptr_t)end); 
#endif

	// Lab 2 memory management initialization functions
	x64_vm_init();
	// Lab 3 user environment initialization functions
	env_init();
	trap_init();
#ifndef VMM_GUEST
	// Lab 4 multiprocessor initialization functions
	mp_init();
	// cprintf("#brk4\n");
	lapic_init();
	// cprintf("#brk5\n");
#endif

	// Lab 4 multitasking initialization functions
	pic_init();
	// cprintf("#brk6\n");
	// Lab 6 hardware initialization functions
	time_init();
	// cprintf("#brk7\n");
	pci_init();
	// cprintf("#brk8\n");
	// Acquire the big kernel lock before waking up APs
	// Your code here:
#ifndef VMM_GUEST
	// Starting non-boot CPUs
	lock_kernel();
	// cprintf("#brk9\n");
	boot_aps();
	// cprintf("#brk10\n");
#endif
	// Should always have idle processes at first.
	// int i;
	// for (i = 0; i < NCPU; i++) {
	// 	cprintf("#start brk11...%d of %d\n", i, NCPU);
	// 	ENV_CREATE(user_idle, ENV_TYPE_IDLE);
	// 	cprintf("#done brk11...%d of %d\n", i, NCPU);
	// }

	cprintf("#brk7\n");
	// Start fs.
	ENV_CREATE(fs_fs, ENV_TYPE_FS);
	// cprintf("#brk12\n");
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE(TEST, ENV_TYPE_USER);
#else
	// Touch all you want.
#if defined(TEST_EPT_MAP)
	test_ept_map();
#endif
	ENV_CREATE(user_icode, ENV_TYPE_USER);
#endif 
	// No environments until lab3

	cprintf("#brk9\n");
	// Should not be necessary - drains keyboard because interrupt has given up.
	// kbd_intr();
	cprintf("#brk10\n");
	// Schedule and run the first user environment!
	sched_yield();
	cprintf("#brk11\n");
}

// While boot_aps is booting a given CPU, it communicates the per-core
// stack pointer that should be loaded by mpentry.S to that CPU in
// this variable.
void *mpentry_kstack;

// Start the non-boot (AP) processors.
static void
boot_aps(void)
{
	extern unsigned char mpentry_start[], mpentry_end[];
	void *code;
	struct Cpu *c;

	// Write entry code to unused memory at MPENTRY_PADDR
	code = KADDR(MPENTRY_PADDR);
	memmove(code, mpentry_start, mpentry_end - mpentry_start);
	// Boot each AP one at a time
	for (c = cpus; c < cpus + ncpu; c++) {
		if (c == cpus + cpunum())  // We've started already.
			continue;

		// Tell mpentry.S what stack to use 
		mpentry_kstack = percpu_kstacks[c - cpus] + KSTKSIZE;
		// Start the CPU at mpentry_start
		lapic_startap(c->cpu_id, PADDR(code));
		// Wait for the CPU to finish some basic setup in mp_main()
		while(c->cpu_status != CPU_STARTED)
			;
	}
}

// Setup code for APs
void
mp_main(void)
{
	// We are in high EIP now, safe to switch to kern_pgdir 
	lcr3(boot_cr3);
	cprintf("SMP: CPU %d starting\n", cpunum());

	lapic_init();
	env_init_percpu();
	trap_init_percpu();
	xchg(&thiscpu->cpu_status, CPU_STARTED); // tell boot_aps() we're up

	// Now that we have finished some basic setup, call sched_yield()
	// to start running processes on this CPU.  But make sure that
	// only one CPU can enter the scheduler at a time!
	//
	// Your code here:
	lock_kernel();
	sched_yield();
	// Remove this after you finish Exercise 4
	// for (;;);
}

/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
const char *panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void
_panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	if (panicstr)
		goto dead;
	panicstr = fmt;

	// Be extra sure that the machine is in as reasonable state
	__asm __volatile("cli; cld");

	va_start(ap, fmt);
	cprintf("kernel panic on CPU %d at %s:%d: ", cpunum(), file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the kernel monitor */
	while (1)
		monitor(NULL);
}

/* like panic, but don't */
void
_warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}
