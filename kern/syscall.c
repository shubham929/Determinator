#if LAB >= 3
/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#if LAB >= 4
#include <kern/sched.h>
#endif

// print a string to the system console.
static void
sys_cputs(char *s)
{
#if SOL >= 3
	page_fault_mode = PFM_KILL;
	printf("%s", TRUP(s));
	page_fault_mode = PFM_NONE;
#else
	printf("%s", s);
#endif
}

// read a character from the system console
static int
sys_cgetc(void)
{
	int c;

	// The cons_getc() primitive doesn't wait for a character,
	// but the sys_cgetc() system call does.
	while ((c = cons_getc()) == 0)
		; /* spin */

	return c;
}

// return the current environment's envid
static u_int
sys_getenvid(void)
{
	return curenv->env_id;
}

// destroy a given environment
// (possibly the currently running environment)
static int
sys_env_destroy(u_int envid)
{
	int r;
	struct Env *e;

	if ((r=envid2env(envid, &e, 1)) < 0)
		return r;
#if LAB >= 5
#else
	if (e == curenv)
		printf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		printf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
#endif
	env_destroy(e);
	return 0;
}

#if LAB >= 4
// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

//
// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
//
// If a page is already mapped at 'va', that page is unmapped as a
// side-effect.
//
// perm -- PTE_U|PTE_P are required, 
//         PTE_AVAIL|PTE_W are optional,
//         but no other bits are allowed (return -E_INVAL)
//
// Return 0 on success, < 0 on error
//	- va must be < UTOP
//	- an environment may modify its own address space or the
//	  address space of its children
//
static int
sys_mem_alloc(u_int envid, u_int va, u_int perm)
{
#if SOL >= 4
	int r;
	struct Env *e;
	struct Page *p;

	if ((r=envid2env(envid, &e, 1)) < 0)
		return r;
	if (((~perm)&(PTE_U|PTE_P)) || (perm&~(PTE_U|PTE_P|PTE_AVAIL|PTE_W)))
		return -E_INVAL;
	if (va >= UTOP)
		return -E_INVAL;
	if ((r=page_alloc(&p)) < 0)
		return r;
	if ((r=page_insert(e->env_pgdir, p, va, perm)) < 0) {
		page_free(p);
		return r;
	}
	memset((void*)page2kva(p), 0, PGSIZE);
	return 0;
#else
	// Your code here.
	panic("sys_mem_alloc not implemented");
#endif
}

// Map the page of memory at 'srcva' in srcid's address space
// at 'dstva' in dstid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_mem_alloc, except 
// that it also must not grant write access to a read-only 
// page.
//
// Return 0 on success, < 0 on error.
//
// Cannot access pages above UTOP.
static int
sys_mem_map(u_int srcid, u_int srcva, u_int dstid, u_int dstva, u_int perm)
{
#if SOL >= 4
	int r;
	struct Env *es, *ed;
	struct Page *p;
	Pte *ppte;

	if (srcva >= UTOP || dstva >= UTOP)
		return -E_INVAL;
	if ((r=envid2env(srcid, &es, 1)) < 0 || (r=envid2env(dstid, &ed, 1)) < 0)
		return r;
	if (((~perm)&(PTE_U|PTE_P)) || (perm&~(PTE_U|PTE_P|PTE_AVAIL|PTE_W)))
		return -E_INVAL;
	if ((p = page_lookup(es->env_pgdir, srcva, &ppte)) == 0)
		return -E_INVAL;
	if ((perm & PTE_W) && !(*ppte & PTE_W))
		return -E_INVAL;
	if ((r = page_insert(ed->env_pgdir, p, dstva, perm)) < 0)
		return r;
	return 0;
#else
	// Your code here.
	panic("sys_mem_map not implemented");
#endif
}

// Unmap the page of memory at 'va' in the address space of 'envid'
// (if no page is mapped, the function silently succeeds)
//
// Return 0 on success, < 0 on error.
//
// Cannot unmap pages above UTOP.
static int
sys_mem_unmap(u_int envid, u_int va)
{
#if SOL >= 4
	int r;
	struct Env *e;

	if ((r=envid2env(envid, &e, 1)) < 0)
		return r;

	if (va >= UTOP)
		return -E_INVAL;

	page_remove(e->env_pgdir, va);
	return 0;
#else
	// Your code here.
	panic("sys_mem_unmap not implemented");
#endif
}

// Allocate a new environment.
//
// The new child is left as env_alloc created it, except that
// status is set to ENV_NOT_RUNNABLE and the register set is copied
// from the current environment.  In the child, the register set is
// tweaked so sys_env_alloc returns 0.
//
// Returns envid of new environment, or < 0 on error.
static int
sys_env_alloc(void)
{
#if SOL >= 4
	int r;
	struct Env *e;

	if ((r=env_alloc(&e, curenv->env_id)) < 0)
		return r;
	e->env_status = ENV_NOT_RUNNABLE;
	e->env_tf = *UTF;
	e->env_tf.tf_eax = 0;
	return e->env_id;
#else
	// Your code here (in lab 4).
	panic("sys_env_alloc not implemented");
#endif
}

// Set envid's trap frame to tf.
//
// Returns 0 on success, < 0 on error.
//
// Return -E_INVAL if the environment cannot be manipulated.
static int
sys_set_trapframe(u_int envid, struct Trapframe *tf)
{
#if SOL >= 4
	int r;
	struct Env *e;
	struct Trapframe ltf;

	page_fault_mode = PFM_KILL;
	ltf = *TRUP(tf);
	page_fault_mode = PFM_NONE;

	ltf.tf_eflags |= FL_IF;
	ltf.tf_cs |= 3;

	if ((r=envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		*UTF = ltf;
	else
		e->env_tf = ltf;
	return 0;
#else
	// Your code here (in lab 4).

	// HINT:
	// Should enforce some limits on tf_eflags and tf_cs
	// The case were envid is the current environment needs 
	//   to be handled specially.

	panic("sys_set_trapframe not implemented");
#endif
}

// Set envid's env_status to status. 
//
// Returns 0 on success, < 0 on error.
// 
// Return -E_INVAL if status is not a valid status for an environment.
static int
sys_set_status(u_int envid, u_int status)
{
#if SOL >= 4
	struct Env *e;
	int r;

	if ((r=envid2env(envid, &e, 1)) < 0)
		return r;
	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
		return -E_INVAL;
	e->env_status = status;
	return 0;
#else
	// Your code here (in lab 4).
	panic("sys_set_status not implemented");
#endif
}

// Set envid's pagefault handler entry point and exception stack.
// (xstacktop points one byte past exception stack).
//
// Returns 0 on success, < 0 on error.
static int
sys_set_pgfault_entry(u_int envid, u_int func)
{
#if SOL >= 4
	int r;
	struct Env *e;

	if ((r=envid2env(envid, &e, 1)) < 0)
		return r;
	e->env_pgfault_entry = func;
	return 0;
#else
	// Your code here.
	panic("sys_set_pgfault_entry not implemented");
#endif
}

// Try to send 'value' to the target env 'envid'.
// If va != 0, then also send page currently mapped at va,
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target has not requested IPC with sys_ipc_recv.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends
//    env_ipc_from is set to the sending envid
//    env_ipc_value is set to the 'value' parameter
// The target environment is marked runnable again.
//
// Return 0 on success, < 0 on error.
//
// If the sender sends a page but the receiver isn't asking for one,
// then no page mapping is transferred but no error occurs.
//
// srcva and perm should have the same restrictions as they had
// in sys_mem_map.
//
// Hint: you will find envid2env() useful.
static int
sys_ipc_can_send(u_int envid, u_int value, u_int srcva, u_int perm)
{
#if SOL >= 4
	int r;
	struct Env *e;
	struct Page *p;

	if ((r=envid2env(envid, &e, 0)) < 0)
		return r;
	if (!e->env_ipc_recving)
		return -E_IPC_NOT_RECV;

	if (srcva != 0 && e->env_ipc_dstva != 0) {

		if (srcva >= UTOP)
			return -E_INVAL;
		if (((~perm)&(PTE_U|PTE_P)) ||
		    (perm&~(PTE_U|PTE_P|PTE_AVAIL|PTE_W)))
			return -E_INVAL;

		p = page_lookup(curenv->env_pgdir, srcva, 0);
		if (p == 0) {
			printf("[%08x] page_lookup %08x failed in sys_ipc_can_send\n",
				curenv->env_id, srcva);
			return -E_INVAL;
		}
		r = page_insert(e->env_pgdir, p, e->env_ipc_dstva, perm);
		if (r < 0)
			return r;

		e->env_ipc_perm = perm;
	} else {
		e->env_ipc_perm = 0;
	}

	e->env_ipc_recving = 0;
	e->env_ipc_from = curenv->env_id;
	e->env_ipc_value = value;
	e->env_status = ENV_RUNNABLE;
	return 0;
#else
	// Your code here
	panic("sys_ipc_can_send not implemented");
#endif
}

// Block until a value is ready.  Record that you want to receive,
// mark yourself not runnable, and then give up the CPU.
//
// Again, dstva should have the same restrictions as it had in
// sys_mem_map.  If it violates these restrictions, assume that it is
// zero.
static void
sys_ipc_recv(u_int dstva)
{
#if SOL >= 4
	if(curenv->env_ipc_recving)
		panic("already recving!");

	if (dstva >= UTOP)
		dstva = 0;

	curenv->env_ipc_recving = 1;
	curenv->env_ipc_dstva = dstva;
	curenv->env_status = ENV_NOT_RUNNABLE;
	sched_yield();
#else
	// Your code here
	panic("sys_ipc_recv not implemented");
#endif
}
#endif	// LAB >= 4


// Dispatches to the correct kernel function, passing the arguments.
uint32_t
syscall(uint32_t sn, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// printf("syscall %d %x %x %x from env %08x\n", sn, a1, a2, a3, curenv->env_id);

#if SOL >= 3
	switch (sn) {
	case SYS_cputs:
		sys_cputs((char*)a1);
		return 0;
	case SYS_cgetc:
		return sys_cgetc();
	case SYS_getenvid:
		return sys_getenvid();
	case SYS_env_destroy:
		return sys_env_destroy(a1);
#if SOL >= 4
	case SYS_yield:
		sys_yield();
		return 0;
	case SYS_mem_alloc:
		return sys_mem_alloc(a1, a2, a3);
	case SYS_mem_map:
		return sys_mem_map(a1, a2, a3, a4, a5);
	case SYS_mem_unmap:
		return sys_mem_unmap(a1, a2);
	case SYS_env_alloc:
		return sys_env_alloc();
	case SYS_set_trapframe:
		return sys_set_trapframe(a1, (struct Trapframe*)a2);
	case SYS_set_status:
		return sys_set_status(a1, a2);
	case SYS_set_pgfault_entry:
		return sys_set_pgfault_entry(a1, a2);
	case SYS_ipc_can_send:
		return sys_ipc_can_send(a1, a2, a3, a4);
	case SYS_ipc_recv:
		sys_ipc_recv(a1);
		return 0;
#endif	// SOL >= 4
	default:
		return -E_INVAL;
	}
#else	// not SOL >= 3
	// Your code here
	panic("syscall not implemented");
#endif	// not SOL >= 3
}

#endif	// LAB >= 3
