# Motivation
While I was still studying in unviersity, I found myself a passion for taking operating system courses. Before, the operating was just a mystical black box. I was simply intrigued by solving the unique challenges of programming an operating system and the opportunity to explore one of humanity's most complex inventions.

Since graduating, I haven't worked on many projects that require low-level interaction with the operating systems, and my knowledge in this area has faded. Additionally, I ran across problems in my work that required a deep understanding of OS concepts to solve. So I decided to take this course to refresh my memory and hopefully get back a hang of how things work at a low level.

# Prerequisite
If you want to complete this assignment on Debian, the dependencies required to run the QEMU simulators need to be installed:
```
sudo apt-get install git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
```

# Assignment

| Labs | Accomplishments |
| --- | ---|
| Lab 1 - UTIL | - Created a user program named `sleep` that invokes system call and pause the shell for a certain period <br> - Created a user program named `pingpong` that exchanges a byte using a pipe <br> - Created a user program named `primes` that implements a **concurrent** pipe-based prime sieve, according to this [paper](https://swtch.com/~rsc/thread/#1) <br> - Created a user program named `find` that recursively searches for files with specific names <br> - Created a user program named `xargs` that converts outputs from stdout to program arguments
| Lab 2 - SYSCALL | - Created a syscall `trace` that prints the invocations of syscalls and return values <br> - Created a syscall `sysinfo` that prints number of active processes and bytes of free memory |
| Lab 3 - PGTBL | - Created a demo program `ugetpid` for quick data sharing between user and kernel. Kernel installs a mapping from USYSCALL to the kernel page that has the data. User can access this memory directly, avoiding `copyout` from a kernel page to a user page.<br> - Created a kernel helper function `vmprint` to print all PTEs in a page table <br> - Created a syscall `pgaccess` to print all pages that are accessed since the last syscall |
| Lab 4 - TRAPS | - Create a kernel helper function `backtrace` to print all return addresses in the call stacks when kernel panics. <br> - Create syscalls `sigalarm` and `sigreturn` that registers a handler to be called when ticks have passed by, and the handler calls `sigreturn` to resume the instruction before the interruption. This example show how callback for an async call can be designed in kernel. `sigalarm` must save the trapframe before interruption and sets `epc` to alarm handler address to make `usertrap` return to alarm handler. `sigreturn` restores the trapframe before interruption.
| Lab 5 - COW | - Modified `fork` syscall to use copy-on-write mechanism. `fork` makes parent and child processes share pages which are made write-protected. When either user process tries to write data to the pages, the hardware triggers an exception which calls the trap handler, which allocates a new page for the faulting process.<br>- Modified `copyout` kernel function to allocate a new page if the dest user page is copy-on-write protected.<br>- Tracked reference count to COW pages, which are added to the freelist when no processs are using it. |
| Lab 6 - THREAD | - Created a program that implements threads with **software-level context switching** enabled. The thread yields voluntarily to another thread.<br>- Designed a program `barrier` with mutex and cv that blocks a thread from executing until enough threads have arrived.<br>- Added mutex per hashtable cell to make a hashtable concurrency-safe |
| LAB 7 - NET | - Added `e1000_transmit` and `e1000_recv` that interacts with a UDP server on host computer |
| Lab 8 - LOCK | - Reduce lock contention by updating the kernel memory allocator to use per-core kernel memory<br>- Reduce lock contention by updating the bcache layer to use hashtable and per-bucket lock |
| Lab 9 - FS | - Supported doubly linked list inode <br> - Added `symlink` syscall to create soft link for a file |
| Lab 10 - MMAP |  - Supported `mmap` and `munmap` that maps a file to a user-controlled virtual memory address. Supported permission control for that virtual memory segment. Also supported writing modifications back to the file with `MAP_SHARED`. |