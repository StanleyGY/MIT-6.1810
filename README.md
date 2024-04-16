# Prerequisite

For Debian users, install the dependencies required by `qemu`:
```
sudo apt-get install git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
```

# Assignment

**Lab 1** - UTIL
- Created a user program named `sleep` that invokes system call and pause the shell for a certain period
- Created a user program named `pingpong` that exchanges a byte using a pipe
- Created a user program named `primes` that implements a **concurrent** pipe-based prime sieve, according to this [paper](https://swtch.com/~rsc/thread/#1)
- Created a user program named `find` that recursively searches for files with specific names
- Created a user program named `xargs` that converts outputs from stdout to program arguments

**Lab 2** - SYSCALL
- Created a syscall `trace` that prints the invocations of syscalls and return values
- Created a syscall `sysinfo` that prints number of active processes and bytes of free memory

**Lab 3** - PGTBL
- Created a demo program `ugetpid` for quick data sharing between user and kernel. Kernel installs a mapping from USYSCALL to the kernel page that has the data. User can access this memory directly, avoiding `copyout` from a kernel page to a user page.
- Created a kernel helper function `vmprint` to print all PTEs in a page table
- Created a syscall `pgaccess` to print all pages that are accessed since the last syscall

**Lab 4** - TRAPS
- Create a kernel helper function `backtrace` to print all return addresses in the call stacks when kernel panics
- Create syscalls `sigalarm` and `sigreturn` that registers a handler to be called when ticks have passed by, and the handler calls `sigreturn` to resume the instruction before the interruption. This example show how callback for an async call can be designed in kernel.
    - `sigalarm` must save the trapframe before interruption and sets `epc` to alarm handler address to make `usertrap` return to alarm handler
    - `sigreturn` restores the trapframe before interruption.

**Lab 5** - COW
- Modified `fork` syscall to use copy-on-write mechanism. `fork` makes parent and child processes share pages which are made write-protected. When either user process tries to write data to the pages, the hardware triggers an exception which calls the trap handler, which allocates a new page for the faulting process.
- Modified `copyout` kernel function to allocate a new page if the dest user page is copy-on-write protected.
- Tracked reference count to COW pages, which are added to the freelist when no processs are using it.

**Lab 6** - THREAD
- Created a program that implements threads with **software-level context switching** enabled. The thread yields voluntarily to another thread.
- Designed a program `barrier` with mutex and cv that blocks a thread from executing until enough threads have arrived
- Added mutex per hashtable cell to make a hashtable concurrency-safe