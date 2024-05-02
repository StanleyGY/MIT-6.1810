//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#ifdef LAB_MMAP
#include "fcntl.h"
#include "memlayout.h"
#endif

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
#ifdef LAB_NET
  else if(ff.type == FD_SOCK){
    sockclose(ff.sock);
  }
#endif
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;

  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  }
#ifdef LAB_NET
  else if(f->type == FD_SOCK){
    r = sockread(f->sock, addr, n);
  }
#endif
  else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  }
#ifdef LAB_NET
  else if(f->type == FD_SOCK){
    ret = sockwrite(f->sock, addr, n);
  }
#endif
  else {
    panic("filewrite");
  }

  return ret;
}

#ifdef LAB_MMAP
int
filemmap(uint64 va) {
  struct proc *p = myproc();

  // Out of bound
  int ind = (va - VMABASE) / PGSIZE;
  if (ind >= NVMA || ind < 0)
    return -1;

  // Remapped
  struct vma *a = &p->vmas[ind];
  if (a->mapped)
    return -1;

  a->mapped = 1;

  // Translate to memory permissions
  int prot = a->prot;
  int perm = PTE_U;
  if (prot && PROT_READ)
    perm |= PTE_R;
  if (prot && PROT_WRITE)
    perm |= PTE_W;
  if (prot && PROT_EXEC)
    perm |= PTE_X;

  // Allocate a physical page
  uint64 pa = (uint64)kalloc();
  if (pa == 0)
    return -1;

  // Zero the page
  memset((void *)pa, 0, PGSIZE);

  // Install PTEs
  if (mappages(p->pagetable, a->start, PGSIZE, (uint64)pa, perm) < 0) {
    kfree((void *)pa);
    return -1;
  }

  // Read file contents
  struct file *f = a->f;
  ilock(f->ip);
  if (readi(f->ip, 1, a->start, a->foffset, PGSIZE) < 0) {
    kfree((void *)pa);
    iunlock(f->ip);
    return -1;
  }
  iunlock(f->ip);
  return 0;
}

int
filemunmap(int i) {
  struct proc *p = myproc();
  struct vma *a = &p->vmas[i];

  if (!a->used)
    return 0;

  struct file *f = a->f;

  // Used but not mapped
  if (!a->mapped) {
    a->used = 0;
    fileclose(f);
    return 0;
  }

  // Write back the modified part to local file
  if (a->flags == MAP_SHARED) {
    // Ideally, only write back pages with dirty bit
    // But this lab doesn't check this
    begin_op();
    ilock(f->ip);
    if (writei(f->ip, 1, a->start, a->foffset, PGSIZE) < 0) {
      iunlock(f->ip);
      end_op();
      return -1;
    }
    iunlock(f->ip);
    end_op();
  }

  // Uninstall PTEs
  uvmunmap(p->pagetable, a->start, 1, 1);

  // Update vma struct
  a->used = 0;
  fileclose(f);
  return 0;
}
#endif