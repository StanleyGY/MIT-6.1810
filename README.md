# Prerequisite

For Debian users, install the dependencies required by `qemu`:
```
sudo apt-get install git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
```

# Assignment

**Lab 1**
- Created a user program named `sleep` that invokes system call and pause the shell for a certain period
- Created a user program named `pingpong` that exchanges a byte using a pipe
- Created a user program named `primes` that implements a **concurrent** pipe-based prime sieve, according to this [paper](https://swtch.com/~rsc/thread/#1)
- Created a user program named `find` that recursively searches for files with specific names
- Created a user program named `xargs` that converts outputs from stdout to program arguments