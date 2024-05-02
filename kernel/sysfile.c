//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#ifdef LAB_MMAP
#include "memlayout.h"
#endif

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();

  // Find inode by the pathname
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  // Cannot add a hard link to a directory
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  // Find the inode for parent directory
  if((dp = nameiparent(new, name)) == 0)
    goto bad;

  ilock(dp);
  // Write the file as an entry in parent directory
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();

  // Get inode for parent directory
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;

  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");

  // Cannot unlink a non-empty dir
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  // Remove from parent directory
  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");

  if(ip->type == T_DIR){
    // Child no longer references to parent via `..`
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  // `dp` for parent and `ip` for child
  struct inode *ip, *dp;
  char name[DIRSIZ];

  // Get inode for the parent directory
  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    // The file has existed already
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    #ifdef LAB_FS
    if (type == T_SYMLINK && ip->type == T_SYMLINK)
      return ip;
    #endif
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    // Allocate an inode for the new file
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) {
    // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  // Link parent directory to child
  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  #ifdef LAB_FS
  if (ip->type == T_SYMLINK && ((omode & O_NOFOLLOW) == 0)) {
    // Follow the symlink
    char target[MAXPATH];
    struct inode *nip = ip;
    int depth = 0;

    while (depth < 10 && ip->type == T_SYMLINK) {

      // Read symlink inode's data, the name of sym-linked file
      if (readi(ip, 0, (uint64)target, 0, sizeof(target)) != sizeof(target)) {
        iunlockput(ip);
        end_op();
        return -1;
      }
      iunlockput(ip);

      // Try open the target path
      if ((nip = namei(target)) == 0) {
        // Sym-linked file does not exist
        end_op();
        return -1;
      }

      ilock(nip);
      if (nip->type != T_SYMLINK) {
        // Find the actual file
        break;
      }

      depth ++;
      ip = nip;
    }

    // Possibly form a cycle
    if (depth >= 10) {
      iunlockput(ip);
      end_op();
      return -1;
    }

    ip = nip;
  }
  #endif

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

#ifdef LAB_NET
int
sys_connect(void)
{
  struct file *f;
  int fd;
  uint32 raddr;
  uint32 rport;
  uint32 lport;

  argint(0, (int*)&raddr);
  argint(1, (int*)&lport);
  argint(2, (int*)&rport);

  if(sockalloc(&f, raddr, lport, rport) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0){
    fileclose(f);
    return -1;
  }

  return fd;
}
#endif

#ifdef LAB_FS
int
sys_symlink(void)
{
  // Create a symbolic link at path that refers to
  // file named by `target`. Does not need the `target` to
  // exist to succeed.
  char target[MAXPATH];
  char path[MAXPATH];
  struct inode *ip;

  if(argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;

  begin_op();

  // Create symbolic file
  ip = create(path, T_SYMLINK, 0, 0);

  // Store the target path in inode's data block
  if (writei(ip, 0, (uint64)target, 0, sizeof(target)) != sizeof(target)) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}
#endif

#ifdef LAB_MMAP
uint64
sys_mmap(void)
{
  // In this lab, assuming:
  // - kernel always decides the VA to map the file
  // - `addr` and `offset` are zero

  int length;    // number of bytes to map
  int prot;      // OPTIONS: readable, writeable, executable
  int flags;     // OPTIONS: MAP_SHARED, MAP_PRIVATE
  int fd;        // the open file descriptor of the file to map

  argint(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);

  struct proc *p = myproc();
  struct file *f = p->ofile[fd];

  // Check if file permission allows `mmap`
  if (!f->readable)
    return -1;

  // You can't write back to a file that is not writable
  if (flags == MAP_SHARED && (prot & PROT_WRITE) && !f->writable)
    return -1;

  // Find an used `vma` struct
  const int req_pages = length / PGSIZE;

  for (int i = 0; i < NVMA; i++) {
    // Count how many pages are available that begins at this address
    int avail_pages = 0;
    for (int j = 0; j < req_pages && i + j < NVMA; j ++)
      if (!p->vmas[i + j].used)
        avail_pages ++;

    if (avail_pages == req_pages) {
      // Mark used for these VMA structs
      for (int j = 0; j < req_pages; j ++) {
        struct vma *a = &p->vmas[i + j];
        a->used = 1;

        // VM info
        a->start = VMABASE + PGSIZE * (i + j);
        a->prot = prot;
        a->flags = flags;
        a->mapped = 0;

        // File info
        a->f = f;
        a->foffset = PGSIZE * j;
        filedup(f);
      }
      return p->vmas[i].start;
    }
  }
  return -1;
}


uint64
sys_munmap(void)
{
  uint64 va;
  int length;

  argaddr(0, &va);
  argint(1, &length);

  // Page-aligned
  if (PGROUNDDOWN(va) != va)
    return -1;

  // Page-aligned
  if (length & 0xfff)
    return -1;

  int start_ind = (va - VMABASE) / PGSIZE;
  int end_ind = (va + length - VMABASE) / PGSIZE;  // exclusive

  // Out of bound
  if (end_ind >= NVMA)
    return -1;

  // Check if the requested ranges are in use
  struct proc *p = myproc();

  for (int i = start_ind; i < end_ind; i ++)
    if (!p->vmas[i].used)
      return -1;

  // Write back the modified part to local file
  for (int i = start_ind; i < end_ind; i ++)
    if (filemunmap(i) < 0)
      return -1;

  return 0;
}
#endif