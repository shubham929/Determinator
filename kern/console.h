/* See COPYRIGHT for copyright information. */

#ifndef PIOS_KERN_CONSOLE_H_
#define PIOS_KERN_CONSOLE_H_
#ifndef PIOS_KERNEL
# error "This is a kernel header; user programs should not #include it"
#endif

#include <inc/types.h>


#define DEBUG_TRACEFRAMES	10

struct iocons;


void cons_init(void);

// Called by device interrupt routines to feed input characters
// into the circular console input buffer.
// Device-specific code supplies 'proc', which polls for a character
// and returns that character or 0 if no more available from device.
void cons_intr(int (*proc)(void));

// Called by init() when the kernel is ready to receive console interrupts.
void cons_intenable(void);

// Called from file_io() in the context of the root process,
// to synchronize the root process's console special I/O files
// with the kernel's console I/O buffers.
// Returns true if I/O was done, false if no new I/O was ready.
bool cons_io(void);

#endif /* PIOS_KERN_CONSOLE_H_ */
