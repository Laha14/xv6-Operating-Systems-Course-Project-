//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//
// for task 3
#define MMAP_FAILED 0xffffffffffffffffUL  

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
// for task 3
#include "memlayout.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
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

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
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

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
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
  if (argfd(0, 0, &f) < 0)
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
  if (argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
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
  if (argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = namei(old)) == 0) {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
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

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
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

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0) {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip)) {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
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

static struct inode *
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if (dp->nlink == 0) {
    iunlockput(dp);
    return 0;
  }

  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0) {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) { // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp->nlink++; // for ".."
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
sys_open(void)  //<-- task 2a modify sys_open to recognise symlink as a new type
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if (omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0) {
      end_op();
      return -1;
    }
  } else {
    if ((ip = namei(path)) == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_SYMLINK){
      int depth=0;
      
      while(ip->type == T_SYMLINK && depth < 10){
        char target[MAXPATH];
        int len = readi(ip, 0, (uint64)target, 0, ip->size);
        if(len < 0 || len >= MAXPATH) {
          iunlockput(ip);
          end_op();
          return -1;
        }
        target[len]='\0';

        iunlockput(ip);
        ip=namei(target);

        if(ip == 0) {
          end_op();
          return -1;
        }
        ilock(ip);
        if(ip->type != T_SYMLINK) break;

        depth++;
      }
      if(depth>=10 && ip->type == T_SYMLINK) {iunlockput(ip); end_op(); return -1;}
    }
    
    if (ip->type == T_DIR && omode != O_RDONLY) {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE) {
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
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
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
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
  if ((argstr(0, path, MAXPATH)) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0) {
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
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR) {
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
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++) {
    if (i >= NELEM(argv)) {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0) {
      goto bad;
    }
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = kexec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
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
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, p->sz, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, p->sz, fdarray + sizeof(fd0), (char *)&fd1,
              sizeof(fd1)) < 0) {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// task 2a symbolic link
uint64
sys_symlink(void){
  char target[MAXPATH], path[MAXPATH];

  argstr(0, target, MAXPATH);
  argstr(1, path, MAXPATH);

  begin_op();

  struct inode *ip = create(path, T_SYMLINK,0,0);
  if(ip==0) {
    end_op();
    return -1;
  }

  int n = strlen(target);
  if(writei(ip, 0, (uint64)target, 0, n) != n) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlockput(ip);

  end_op();

  return 0;
}

// ==for task 3
static struct vma *find_vma(struct proc *p, uint64 va) {
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(v->used && va >= v->addr && va < v->addr + v->len)
      return v;
  }
  return 0;
}

// Write one resident page of a MAP_SHARED vma back to its file.
// Never extends the file. Chunked so each transaction fits in the log.
static void writeback_page(struct vma *v, uint64 va, uint64 pa) {
  uint64 n = v->addr + v->len - va;
  if(n > PGSIZE) n = PGSIZE;
  uint64 off = v->offset + (va - v->addr);
  int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;

  for(uint64 i = 0; i < n; ){
    uint64 n1 = n - i;
    if(n1 > (uint64)max) n1 = max;
    begin_op();
    ilock(v->f->ip);
    if(off + i >= v->f->ip->size){        // past EOF: nothing to write
      iunlock(v->f->ip);
      end_op();
      break;
    }
    if(off + i + n1 > v->f->ip->size)
      n1 = v->f->ip->size - (off + i);
    int r = writei(v->f->ip, 0, pa + i, off + i, n1);  // kernel src (direct map)
    iunlock(v->f->ip);
    end_op();
    if(r <= 0) break;
    i += r;
  }
}

// Unmap [addr, addr+n) inside vma v (write back if shared+dirty),
// then shrink/remove the vma. Range is page-aligned.
static void vma_unmap(struct proc *p, struct vma *v, uint64 addr, uint64 n) {
  for(uint64 va = addr; va < addr + n; va += PGSIZE){
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte == 0 || (*pte & PTE_V) == 0)
      continue;                            // never faulted in
    if((v->flags & MAP_SHARED) && (*pte & PTE_D))
      writeback_page(v, va, PTE2PA(*pte));
    uvmunmap(p->pagetable, va, 1, 1);      // kfree -> refcount--
  }
  if(addr == v->addr){                     // trimmed from the start
    v->addr += n;
    v->offset += n;
  }
  v->len -= n;
  if(v->len == 0){
    fileclose(v->f);
    v->used = 0;
  }
}

void vma_cleanup(struct proc *p) {
  for(int i = 0; i < NVMA; i++)
    if(p->vmas[i].used)
      vma_unmap(p, &p->vmas[i], p->vmas[i].addr, p->vmas[i].len);
}

uint64 sys_mmap(void) {
  uint64 addr;
  int len, prot, flags, fd, offset;
  struct file *f;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argint(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  if(argfd(4, &fd, &f) < 0)
    return -1;
  argint(5, &offset);
  
  if(len <= 0 || addr != 0 || offset != 0)
    return MMAP_FAILED;
  if(((flags & MAP_SHARED) != 0) == ((flags & MAP_PRIVATE) != 0))
    return MMAP_FAILED;                      // both or neither
  if((prot & PROT_READ) && !f->readable)
    return MMAP_FAILED;
    // writing back to a read-only file is forbidden; private writes are fine
  if((flags & MAP_SHARED) && (prot & PROT_WRITE) && !f->writable)
    return MMAP_FAILED;

  uint64 len_r = PGROUNDUP(len);

  struct vma *v = 0;
  uint64 top = TRAPFRAME;
  for(int i = 0; i < NVMA; i++){
    if(p->vmas[i].used){
      if(p->vmas[i].addr < top) top = p->vmas[i].addr;
    } else if(v == 0){
      v = &p->vmas[i];
    }
  }
  if(v == 0 || top - len_r < p->sz)       // no slot or collides with heap/stack
    return -1;

  v->used   = 1;
  v->addr   = top - len_r;
  v->len    = len_r;
  v->prot   = prot;
  v->flags  = flags;
  v->offset = offset;
  v->f      = filedup(f);                 // keep file alive after close(fd)
  return v->addr;
}

uint64 sys_munmap(void) {
  uint64 addr;
  int len;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argint(1, &len);
  if(len <= 0 || addr % PGSIZE != 0)
    return -1;

  struct vma *v = find_vma(p, addr);
  if(v == 0)
    return -1;

  uint64 n = PGROUNDUP(len);
  if(addr == v->addr){                    // start (or whole region)
    if(n > v->len) n = v->len;
  } else if(addr + n >= v->addr + v->len){ // tail
    n = v->addr + v->len - addr;
  } else {
    return -1;                            // hole punching not supported
  }
  vma_unmap(p, v, addr, n);
  return 0;
}

// Called from usertrap on load (13) / store (15) page faults.
// Returns 0 if the fault was resolved.
int mmap_handler(uint64 va, int cause) {
  struct proc *p = myproc();
  struct vma *v = find_vma(p, va);
  if(v == 0)
    return -1;
  if(cause == 15 && !(v->prot & PROT_WRITE))
    return -1;

  va = PGROUNDDOWN(va);
  pte_t *pte = walk(p->pagetable, va, 0);
  if(pte && (*pte & PTE_V))
    return -1;                            // already mapped: a real permission fault

  char *mem = kalloc();
  if(mem == 0)
    return -1;
  memset(mem, 0, PGSIZE);                 // zero-fills the tail past EOF

  ilock(v->f->ip);
  readi(v->f->ip, 0, (uint64)mem, v->offset + (va - v->addr), PGSIZE);
  iunlock(v->f->ip);

  int perm = PTE_U;
  if(v->prot & PROT_READ)  perm |= PTE_R;
  if(v->prot & PROT_WRITE) perm |= PTE_R | PTE_W;  // W without R is illegal in RISC-V
  if(v->prot & PROT_EXEC)  perm |= PTE_X;

  if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0){
    kfree(mem);
    return -1;
  }
  return 0;
}

// fork helper: replicate already-resident mmap pages into child.
int vma_copy_pages(struct proc *p, struct proc *np) {
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(!v->used) continue;
    for(uint64 va = v->addr; va < v->addr + v->len; va += PGSIZE){
      pte_t *pte = walk(p->pagetable, va, 0);
      if(pte == 0 || (*pte & PTE_V) == 0)
        continue;
      uint64 pa = PTE2PA(*pte);
      int flags = PTE_FLAGS(*pte) & (PTE_R | PTE_W | PTE_X | PTE_U);

      if(v->flags & MAP_SHARED){          // same physical page, refcount++
        kincref((void*)pa);
        if(mappages(np->pagetable, va, PGSIZE, pa, flags) != 0){
          kfree((void*)pa);
          return -1;
        }
      } else {                            // private: own copy
        char *mem = kalloc();
        if(mem == 0) return -1;
        memmove(mem, (char*)pa, PGSIZE);
        if(mappages(np->pagetable, va, PGSIZE, (uint64)mem, flags) != 0){
          kfree(mem);
          return -1;
        }
      }
    }
  }
  return 0;
}
// ==

