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
#include "fcntl.h"
#include "memlayout.h"

struct devsw devsw[NDEV];
struct vma vma_list[VMA_SIZE];
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
  } else {
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
  } else {
    panic("filewrite");
  }

  return ret;
}
struct vma*
alloc_vma() {
  for (int i = 0; i < VMA_SIZE; i++) {
    acquire(&vma_list[i].lock);
    if (!vma_list[i].used) {
      vma_list[i].used = 1;
      release(&vma_list[i].lock);
      return &vma_list[i];
    } else {
      release(&vma_list[i].lock);
    }
  }
  panic("no free vma");
}
void *mmap(void *addr, uint64 length, int prot, int flags,
           int fd, uint64 offset) {
  struct file *f;
  if (fd < 0 || fd >=NOFILE || (f=myproc()->ofile[fd]) == 0)
    return (void *)0xffffffffffffffff;
  if((prot & PROT_WRITE) && !f->writable && !(flags & MAP_PRIVATE))
    return (void *)0xffffffffffffffff;
  if((prot & PROT_READ) && !f->readable)
    return (void *)0xffffffffffffffff;
  struct vma *v = alloc_vma();
  struct proc *p = myproc();
  v->length = length;
  v->permission = prot;
  v->flags = flags;
  v->offset = offset;
  v->f = f;
  v->f->off = offset;
  filedup(v->f);
  if (p->vma) {
    // printf("append vma\n");
    struct vma *next_vma = p->vma;
    while (next_vma->next) next_vma = next_vma->next;
    next_vma->next = v;
    v->addr_start = PGROUNDDOWN(next_vma->addr_start - length);
    v->addr_end = v->addr_start + length;
  } else {
    // printf("first vma\n");
    v->addr_start = TRAPFRAME - length;
    v->addr_end = v->addr_start + length;
    p->vma = v;
  };
  // printf("addr_start: %x\n", v->addr_start);
  return (void *)v->addr_start;
}
void mmap_miss_page_handler(uint64 va) {
  va = PGROUNDDOWN(va);
  // printf("load page %x, pid: %d\n", va, myproc()->pid);
  struct proc *p = myproc();
  struct vma *v = p->vma;
  while (v) {
    // printf("miss page, addr range: [%x, %x], pid: %d\n", v->addr_start, v->addr_end, myproc()->pid);
    if (va >= v->addr_start && va < v->addr_end)
      break;
    else
      v = v->next;
  }
  if (v) {
    char *mem = kalloc();
    memset(mem, 0, PGSIZE);
    int permission = PTE_U | ((v->permission & PROT_WRITE) ? PTE_W : 0) |
                     ((v->permission & PROT_READ) ? PTE_R : 0) |
                     ((v->permission & PROT_EXEC) ? PTE_X : 0);
    mappages(p->pagetable, va, PGSIZE, (uint64)mem, permission);
    v->f->off = (va - v->addr_start + v->offset);
    // printf("before read off: %d\n", v->f->off);
    fileread(v->f, va, PGSIZE);
    // printf("after read f: %d to va: %x, f->off:%d\n", v->f->ip->inum, va, v->f->off);
    // uint64 pa = walkaddr(p->pagetable, va);
    // char * pc = (char *) pa;
    // printf("p[0]:%x, p[1]: %x, p[2]: %x, p[3]: %x\n", *pc, *(pc+1), *(pc+2), *(pc+3));
  }
}

int munmap(uint64 va, int length) {
  va = PGROUNDDOWN(va);
  struct proc *p = myproc();
  struct vma *v = p->vma;
  struct vma *pre = 0;
    while (v) {
      // printf("addr range: [%x, %x], pid: %d\n", v->addr_start, v->addr_end, myproc()->pid);
      if (v->used && va >= v->addr_start && va < v->addr_end) break;
      else {
        pre = v;
        v = v->next;
      }
    }
  if (v) {
    if (v->flags & MAP_SHARED) {
      v->f->off = v->offset;
      // printf("need write back, v->f: %d, f->off: %d, va: %x, len:%d, pid: %d\n",
        // v->f->ip->inum, v->f->off, va, length, myproc()->pid);
      filewrite(v->f, va, length);
    }
    v->length -= length;
    if (v->length < 0 ) panic("unmap length");
    else if (v->length == 0) {
      fileclose(v->f);
      if (pre == 0) p->vma = v->next;
      else pre->next = v->next;
      v->next = 0;
      v->used = 0;
      // printf("vma length = 0\n");
    } else {
      v->addr_start += length;
      v->offset +=  length;
    }
    if (walkaddr(p->pagetable, va)) uvmunmap(p->pagetable, va, length / PGSIZE, 1);
  } else return -1;
  return 0;
}
void vma_free(struct proc *p) {
  struct vma *v = p->vma;
  struct vma *pre = 0;
  while (v) {
    // printf("free addr range: [%x, %x]\n", v->addr_start, v->addr_end);
    if (v->flags & MAP_SHARED) {
      v->f->off = v->offset;
      // printf("write back, v->f: %d, f->off: %d, va: %x, len:%d, pid: %d\n",
        // v->f->ip->inum, v->f->off, v->addr_start, v->length, p->pid);
      filewrite(v->f, v->addr_start, v->length);
    }
    fileclose(v->f);
    v->used = 0;
    pre = v;
    if (walkaddr(p->pagetable, v->addr_start))
      uvmunmap(p->pagetable, v->addr_start, v->length / PGSIZE, 1);
    v = v->next;
    pre->next = 0;
  }
  // printf("pid exit :%d\n", p->pid);
}
void fork_vma(struct proc *np, struct proc *p) {
  np->vma = 0;
  struct vma *pv = p->vma;
  struct vma *npv;
  struct vma *np_pre_vma_list = np->vma;
  while (pv) {
    npv = alloc_vma();
    acquire(&pv->lock);
    npv->addr_start = pv->addr_start;
    npv->addr_end = pv->addr_end;
    npv->length = pv->length;
    npv->permission = pv->permission;
    npv->f = pv->f;
    npv->f->off = pv->offset;
    filedup(npv->f);
    npv->flags = pv->flags;
    npv->offset = pv->offset;
    if (np_pre_vma_list == 0) {
      np_pre_vma_list = npv;
      np->vma = np_pre_vma_list;
    } else {
      np_pre_vma_list->next = npv;
      np_pre_vma_list = np_pre_vma_list->next;
    }
    release(&pv->lock);
    // printf("pv: %d\n", pv->length);
    pv = pv->next;
  }
  // if (np->vma) printf("vp_vma_len: %d\n", np->vma->length); else printf("no vma\n");
}
