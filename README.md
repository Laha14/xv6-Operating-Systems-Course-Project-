# xv6 Operating Systems Course Project (CS-303)

This repository contains our Operating Systems course project, built on the **RISC-V** port of xv6 (`xv6-riscv`). The project has three tasks, each documented separately below:

| Task | Topic |
|---|---|
| Task 1 | Periodic user-level alarms (`sigalarm`, `sigreturn`) |
| Task 2A | Large files (doubly-indirect blocks) |
| Task 2B | Symbolic links (`symlink`) |
| Task 3 | Memory-mapped files (`mmap`, `munmap`) |

System call numbers added by the project (`kernel/syscall.h`):

| Number | System call |
|---|---|
| 23 | `sigalarm` |
| 24 | `sigreturn` |
| 25 | `symlink` |
| 26 | `mmap` |
| 27 | `munmap` |

---

## Task 1: Periodic User-Level Alarms

### 1. Project Overview

Task 1 adds periodic user-level alarms. A process asks the kernel to call a handler in user space after every `n` timer ticks of CPU time it consumes.

Two system calls are introduced:

- `sigalarm(interval, handler)` — arranges for `handler` to run after every `interval` ticks. An interval of `0` disables future alarms.
- `sigreturn()` — called at the end of the handler. It restores the interrupted program's registers so it resumes exactly where it left off.

While a handler is running, further alarms are held off (no re-entrant handlers).

### 2. Files Modified

| File | Purpose |
|---|---|
| `kernel/proc.h` | Added alarm fields to `struct proc`. |
| `kernel/proc.c` | Initialised alarm fields in `allocproc()`; freed the saved trap frame in `freeproc()`. |
| `kernel/syscall.h` | Assigned system call numbers 23 and 24. |
| `kernel/syscall.c` | Declared and registered `sys_sigalarm` / `sys_sigreturn`. |
| `kernel/sysproc.c` | Implemented `sys_sigalarm()` and `sys_sigreturn()`. |
| `kernel/trap.c` | Added alarm logic to `usertrap()` for timer interrupts. |
| `user/user.h` | Declared `sigalarm()` and `sigreturn()` for user programs. |
| `user/usys.pl` | Added user-space stub generators (`usys.S` is generated from this). |
| `user/alarmtest.c`, `user/alarmdisable.c` | New test programs. |
| `Makefile` | Added the test programs to `UPROGS`. |

### 3. Process Structure Changes — `kernel/proc.h`

```c
// alarm fields related to task 1
int interval, ticks, active;
uint handler;
struct trapframe *store_copy;
```

- `interval`: ticks between alarms (`0` means disabled).
- `ticks`: ticks counted since the last alarm.
- `active`: set to `1` while a handler is running; prevents nested alarms.
- `handler`: user-space address of the handler function.
- `store_copy`: page holding a copy of the interrupted trap frame.

### 4. Process Initialisation and Cleanup — `kernel/proc.c`

In `allocproc()` a new process starts with alarms disabled:

```c
p->interval = 0;
p->ticks = 0;
p->handler = 0;
p->active = 0;
p->store_copy = 0;
```

In `freeproc()` any saved trap frame is released so it cannot leak:

```c
if (p->store_copy)
  kfree((void *)p->store_copy);
p->store_copy = 0;
```

### 5. System Call Declarations and Registration

`kernel/syscall.h`:

```c
#define SYS_sigalarm  23
#define SYS_sigreturn 24
```

`kernel/syscall.c`:

```c
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);
...
[SYS_sigalarm]  = sys_sigalarm,
[SYS_sigreturn] = sys_sigreturn,
```

`user/user.h`:

```c
int sigalarm(int interval, void (*handler)(void));
int sigreturn(void);
```

`user/usys.pl`:

```perl
entry("sigalarm");
entry("sigreturn");
```

### 6. Implementing `sigalarm()` — `kernel/sysproc.c`

`sys_sigalarm()`:

1. Reads `interval` and `handler` from the argument registers.
2. Returns `-1` for a negative interval.
3. Stores the interval and handler in the current process and resets `ticks` to `0`.
4. If `interval == 0`, clears `active`, which disables the alarm.

### 7. Implementing `sigreturn()` — `kernel/sysproc.c`

`sys_sigreturn()`:

1. Returns `-1` if there is no saved trap frame.
2. Reads the interrupted program's original `a0` from the saved copy.
3. Copies the saved trap frame (all registers, `epc`, `sp`, ...) back over `p->trapframe`.
4. Frees the saved copy, clears `store_copy` and resets `active`.
5. Returns the saved `a0`.

Step 5 matters. After a system call, `syscall()` writes the return value into `a0`. Returning the saved `a0` makes that write put back the interrupted program's own value, so `a0` is restored too.

### 8. Timer Interrupt Handling — `kernel/trap.c`

`usertrap()` runs after the trap is dispatched. If it was a timer interrupt (`which_dev == 2`) and `interval > 0` and `active == 0`:

1. `p->ticks` is incremented.
2. When `ticks >= interval`, one page is allocated with `kalloc()`.
3. The current trap frame is copied into that page (`store_copy`).
4. `active` is set to `1` and `ticks` reset to `0`.
5. `p->trapframe->epc` is set to the handler address. On return to user mode, execution continues in the handler instead of the interrupted instruction.

The interrupted `epc` is saved unchanged in the copy, so `sigreturn()` resumes at the exact interrupted instruction.

### 9. Test Programs

#### `user/alarmtest.c` — Basic periodic alarm

Sets `sigalarm(10, handler)`, spins until the handler has run three times, disables the alarm, and exits. Each handler call increments a counter, prints, and calls `sigreturn()`.

**Expected output:**

```text
Alarm fired! count=1
Alarm fired! count=2
Alarm fired! count=3
Alarm test finished!
```

#### `user/alarmdisable.c` — Disabling alarms

Waits for one alarm, calls `sigalarm(0, handler)`, runs a long busy loop, and checks that the counter did not change.

**Expected output:**

```text
Alarm fired!
PASS: Alarm disabled
```

These tests cover periodic firing, disabling, and returning to the main loop. Register preservation is covered by the design (full trap frame copy) but is not directly tested by a register-clobbering test.

### 10. Building and Running

From the project root:

```bash
make clean
make qemu
```

(`make qemu-nox` is not a target of this xv6 version; use `make qemu`.) At the xv6 shell:

```text
alarmtest
alarmdisable
```

The tests are listed in `UPROGS` in the `Makefile`:

```makefile
$U/_alarmtest\
$U/_alarmdisable\
```

### 11. Implementation Summary

- **Timer tracking:** each timer interrupt in user mode increments `p->ticks`.
- **Handler invocation:** at the interval, `epc` is redirected to the handler.
- **Nested-handler prevention:** `active` blocks new alarms until `sigreturn()`.
- **Execution restoration:** the full trap frame is saved and restored, including `a0`.
- **Disabling:** `sigalarm(0, ...)` stops further alarms.

---

## Task 2A: File-System Enhancements — Large Files

### 1. Project Overview

Stock xv6 inodes hold 12 direct block numbers and one singly-indirect block, for a maximum of 12 + 256 = 268 blocks. Task 2A adds a **doubly-indirect** block while keeping the on-disk inode the same size.

New layout of `ip->addrs[]` (13 entries):

| Index | Meaning |
|---|---|
| 0 – 10 | 11 direct blocks |
| 11 | singly-indirect block (256 entries) |
| 12 | doubly-indirect block (256 × 256 entries) |

Maximum file size: 11 + 256 + 65,536 = **65,803 blocks** (≈ 64 MB).

### 2. Files Modified

| File | Purpose |
|---|---|
| `kernel/fs.h` | New `NDIRECT`, `NDINDIRECT`, `MAXFILE`; `addrs[NDIRECT+2]` in `struct dinode`. |
| `kernel/file.h` | `addrs[NDIRECT+2]` in the in-memory `struct inode`. |
| `kernel/fs.c` | `bmap()` and `itrunc()` extended to the doubly-indirect level. |
| `kernel/param.h` | `FSSIZE` raised from 2000 to 200000 blocks so the big file fits. |
| `user/bigfile.c` | New test program. |
| `Makefile` | Added `_bigfile` to `UPROGS`. |

### 3. Inode Definition Changes

`kernel/fs.h`:

```c
#define NDIRECT    11
#define NINDIRECT  (BSIZE / sizeof(uint))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE    (NDIRECT + NINDIRECT + NDINDIRECT)

struct dinode {
  ...
  uint addrs[NDIRECT + 2];   // 11 direct + 1 indirect + 1 double-indirect
};
```

Size check: the old array had `12 + 1 = 13` entries; the new one has `11 + 2 = 13`. The on-disk inode size is unchanged, as the assignment requires. `kernel/file.h` is updated to match.

### 4. Block Mapping — `bmap()` in `kernel/fs.c`

`bmap(ip, bn)` returns the disk block for file block `bn`, allocating on demand:

1. `bn < 11`: use `addrs[bn]`.
2. `bn < 11 + 256`: use the block at `addrs[11]`, entry `bn - 11`.
3. Otherwise let `bn' = bn - 11 - 256`:
   - load (or allocate) the doubly-indirect block at `addrs[12]`;
   - `outer = bn' / 256` selects an entry; allocate the singly-indirect block there if empty;
   - `inner = bn' % 256` selects the data block inside it; allocate if empty.

Every modified indirect block is passed to `log_write()` so the update is part of the file-system transaction.

### 5. Freeing Blocks — `itrunc()` in `kernel/fs.c`

`itrunc()` frees, in order: the direct blocks; the data blocks of the singly-indirect block and then that block; and for the doubly-indirect block, for each non-zero entry, all data blocks it points to, the singly-indirect block itself, and finally the doubly-indirect block. It then sets `size = 0` and calls `iupdate()`.

### 6. Rebuilding the File System

Because the inode layout changed, `fs.img` must be rebuilt (`mkfs` includes `kernel/fs.h`). `FSSIZE` was raised to 200000 blocks (~200 MB image) so a file of 65,803 blocks plus metadata fits. The Makefile rule `mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h` rebuilds `mkfs` automatically when those headers change. Run `make clean` before the first build with the new layout.

### 7. Test Program — `user/bigfile.c`

The test creates `testfile`, writes `MAXFILE` blocks (block `i` filled with byte `i % 251`), closes and reopens it, reads every block back and verifies the contents, then unlinks the file (exercising `itrunc()` on all three levels).

**Expected output (abridged):**

```text
Testing maximum file size: 65803 blocks
Writing: 4096/65803 blocks complete
...
Writing: 65803/65803 blocks complete
Writing completed. Checking file...
...
All 65803 blocks verified successfully!
Test file deleted successfully.
```

### 8. Building and Running

```bash
make clean
make qemu
```

Then at the shell: `bigfile`. The test is slow because it writes about 64 MB through the log.

### 9. Implementation Summary

- 11 direct + 1 single-indirect + 1 double-indirect pointers, same inode size.
- `bmap()` allocates at every level on demand; `itrunc()` frees at every level.
- Maximum file size is 65,803 blocks.

---

## Task 2B: File-System Enhancements — Symbolic Links

### 1. Project Overview

Task 2B adds symbolic (soft) links. A symlink is an inode of a new type whose data is the **pathname** of its target. When `open()` meets a symlink, the kernel reads the stored path and opens that path instead. Unlike hard links, symlinks can cross file systems and refer to directories, and they can dangle.

New system call:

```c
int symlink(char *target, char *path);   // 0 on success, -1 on failure
```

### 2. Files Modified

| File | Purpose |
|---|---|
| `kernel/stat.h` | Added `T_SYMLINK` (type 4). |
| `kernel/syscall.h` | `SYS_symlink` = 25. |
| `kernel/syscall.c` | Declared and registered `sys_symlink`. |
| `kernel/sysfile.c` | Implemented `sys_symlink()`; made `sys_open()` follow symlinks. |
| `user/user.h`, `user/usys.pl` | User-space declaration and stub. |
| `user/symlinktest.c` | New test program. |
| `Makefile` | Added `_symlinktest` to `UPROGS`. |

### 3. New Inode Type — `kernel/stat.h`

```c
#define T_SYMLINK 4   // Soft link
```

### 4. Implementing `symlink()` — `kernel/sysfile.c`

`sys_symlink()`:

1. Reads `target` and `path` from user space.
2. Starts a file-system transaction (`begin_op()`).
3. Creates a new inode of type `T_SYMLINK` at `path` with `create()`. If `path` already exists, `create()` fails and the call returns `-1`.
4. Writes the target string (without the terminating NUL) into the inode with `writei()`.
5. Unlocks and releases the inode and ends the transaction (`end_op()`).

### 5. Following Links — `sys_open()` in `kernel/sysfile.c`

After `namei()` finds the inode and it is locked, if its type is `T_SYMLINK`:

1. Read the stored path with `readi()`; fail if it is too long.
2. Release the link inode and look up the stored path with `namei()`.
3. Repeat while the result is still a symlink.
4. Stop after **10** hops and fail. This detects cycles such as `A -> B -> A`.
5. If a target does not exist (dangling link), `open()` fails with `-1`.

Chained links (`link2 -> link1 -> file`) work through the same loop.

### 6. Test Program — `user/symlinktest.c`

| Test | Checks |
|---|---|
| 1 | Open a symlink to a regular file and read its contents. |
| 2 | Open a symlink to another symlink (chain). |
| 3 | Opening a dangling link fails. |
| 4 | Opening a circular link pair fails (no infinite loop). |

**Expected output:**

```text
Starting symlink tests...
Test 1: PASS
Test 2: PASS
Test 3 (broken link): PASS
Test 4 (circular links): PASS
Symlink tests completed.
```

### 7. Building and Running

```bash
make clean
make qemu
```

Then at the shell: `symlinktest`. Note that the test leaves `original.txt`, `link1`, `link2`, `broken`, `cycleA` and `cycleB` in the file system; remove them with `rm` before re-running.

### 8. Implementation Summary

- Symlinks are ordinary inodes of type `T_SYMLINK` whose data is the target path.
- `sys_open()` resolves links with a bounded loop (10 hops).
- `symlink()` returns `0` on success and `-1` on failure.

---

## Task 3: Memory-Mapped Files

### 1. Project Overview

Task 3 adds file-backed memory mappings:

```c
void *mmap(void *addr, int len, int prot, int flags, int fd, int offset);
int   munmap(void *addr, int len);
```

Assumptions from the assignment: `addr` is 0 (kernel chooses), `offset` is 0, `prot` is `PROT_READ` and/or `PROT_WRITE`, and `flags` is `MAP_SHARED` or `MAP_PRIVATE`. `mmap` returns `0xffffffffffffffff` on failure.

Pages are loaded **lazily**: `mmap` only records the mapping; physical memory is allocated and the file read when the program first touches a page (page fault).

### 2. Files Modified

| File | Purpose |
|---|---|
| `kernel/proc.h` | `struct vma` and `vmas[NVMA]` in `struct proc`. |
| `kernel/proc.c` | Initialise VMAs in `allocproc()`; copy them in `fork()`; clean up in `kexit()`. |
| `kernel/sysfile.c` | `sys_mmap`, `sys_munmap`, `mmap_handler`, `vma_cleanup`, `vma_copy_pages`. |
| `kernel/vm.c` | `vmfault()` calls `mmap_handler()`; `copyout()` marks pages dirty. |
| `kernel/kalloc.c` | Per-page reference counts (`kincref`); `kfree` only frees at count 0. |
| `kernel/exec.c` | Unmap all mappings at the start of `exec`. |
| `kernel/fcntl.h` | `PROT_*` and `MAP_*` constants. |
| `kernel/defs.h` | Prototypes for the new functions. |
| `kernel/syscall.h`, `kernel/syscall.c` | `SYS_mmap` = 26, `SYS_munmap` = 27. |
| `user/user.h`, `user/usys.pl` | User declarations and stubs. |
| `user/mmaptest1.c`, `user/mmaptest2.c` | New test programs. |
| `Makefile` | Added both tests to `UPROGS`. |

### 3. Data Structures

`kernel/proc.h`:

```c
#define NVMA 16

struct vma {
  int used;
  uint64 addr;        // page-aligned start
  uint64 len;         // page-rounded length
  int prot;
  int flags;
  struct file *f;
  uint64 offset;      // file offset corresponding to addr
};
```

Each process has `struct vma vmas[NVMA];`, so up to 16 mappings.

`kernel/fcntl.h`:

```c
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
```

### 4. Implementing `mmap()` — `kernel/sysfile.c`

`sys_mmap()`:

1. Reads the arguments and fetches the `struct file` for `fd`; returns failure for a bad fd.
2. Rejects `len <= 0`, non-zero `addr` or `offset`, and flags that are not exactly one of `MAP_SHARED` / `MAP_PRIVATE`.
3. Rejects `PROT_READ` on a file not opened for reading, and `MAP_SHARED` + `PROT_WRITE` on a file not opened for writing. (`MAP_PRIVATE` + `PROT_WRITE` on a read-only file is allowed, since writes never reach the file.)
4. Chooses the address: the region goes just below the lowest existing mapping (or just below `TRAPFRAME` for the first one), growing downward, and must stay above `p->sz`.
5. Fills a free `vma` slot and takes a reference on the file with `filedup()`, so the mapping survives `close(fd)`.
6. Returns the start address. No memory is allocated and no file data is read.

### 5. Lazy Loading — page fault path

`usertrap()` sends load (13) and store (15) page faults to `vmfault()`. `vmfault()` first calls `mmap_handler()`, which:

1. Finds the VMA containing the faulting address (fails if none, or if a store hits a read-only mapping).
2. Allocates a zeroed page, which also zero-fills the tail past end-of-file.
3. Reads the corresponding page of the file with `readi()`.
4. Maps the page with permissions from `prot` (`PTE_U`, plus `PTE_R` / `PTE_W` / `PTE_X`).

The same path is used when the kernel itself touches a mapped page through `copyin()` / `copyout()` (for example `read(fd, mapped_addr, n)`).

### 6. Dirty Tracking — `kernel/vm.c`

Hardware sets `PTE_D` on user stores. Kernel stores through `copyout()` bypass the MMU, so `copyout()` sets `PTE_D` explicitly. This lets `munmap` and exit write back pages that the kernel modified.

### 7. Implementing `munmap()` — `kernel/sysfile.c`

`sys_munmap(addr, len)` requires a page-aligned `addr` inside a mapping. It supports unmapping the start, the end, or the whole region (no holes in the middle). For each page in the range, `vma_unmap()`:

1. Skips pages that were never faulted in.
2. For `MAP_SHARED` pages with `PTE_D` set, writes the page back to the file with `writei()`. It never extends the file and writes in chunks small enough to fit in one log transaction.
3. Unmaps the page and frees it (`uvmunmap` → `kfree`).

Then the VMA is shrunk (start trimmed: `addr` and `offset` advance; end trimmed: `len` decreases). When `len` reaches 0, the file reference is dropped with `fileclose()` and the slot is freed.

### 8. Reference Counting — `kernel/kalloc.c`

`MAP_SHARED` mappings in a parent and child must point at the same physical page. Each physical page therefore has a reference count:

- `kalloc()` sets the count to 1.
- `kincref()` increments it.
- `kfree()` decrements it and only returns the page to the free list at 0.

### 9. Process Lifecycle

- **`fork()`:** VMA entries are copied and each file is `filedup()`'d. Pages already resident are replicated: `MAP_SHARED` pages are shared (`kincref`), `MAP_PRIVATE` pages are copied into new pages. On failure the child's mappings are cleaned up.
- **`kexit()`:** `vma_cleanup()` unmaps every region, writing back dirty `MAP_SHARED` pages, as if `munmap` had been called.
- **`exec()`:** `vma_cleanup()` runs before the old address space is replaced.

### 10. Test Programs

#### `user/mmaptest1.c`

Prints `ok: <name>` for: read-only mapping; private write not saved; shared write saved at `munmap`; partial `munmap` (start then rest) with write-back; and write-back when a forked child exits without calling `munmap`. Ends with `all tests OK`.

#### `user/mmaptest2.c`

A larger suite. Output:

```text
mmaptest starting
  read/private ............ ok
  permissions / bad args .. ok
  private write ........... ok
  shared write-back ....... ok
  region longer than file . ok
  partial munmap .......... ok
  fork + MAP_SHARED ....... ok
  ...
mmaptest: all tests OK
```

Some tests deliberately touch unmapped memory in a child process. The kernel prints `usertrap(): unexpected scause ...` for those. This is expected.

### 11. Building and Running

```bash
make clean
make qemu
```

Then at the shell: `mmaptest1` and `mmaptest2`. Run `usertests` afterwards to check that nothing else broke.

### 12. Implementation Summary

- **Lazy allocation:** `mmap` only records a VMA; pages appear on first access.
- **Shared vs. private:** shared pages are written back on `munmap` / exit; private pages never are.
- **Partial unmapping:** start, end or whole region.
- **Lifecycle:** mappings are inherited by `fork`, released on `exit` and `exec`.
- **Reference counting:** lets parent and child share `MAP_SHARED` pages safely.

---

## Known Limitations

These are behaviours of the current code that a reader should be aware of.

- **Task 1:** `handler` is stored as a 32-bit `uint`; RISC-V user addresses in xv6 are small enough that this works, but `uint64` is the correct type. The tests do not include a register-clobbering test.
- **Task 2A:** `bmap()` does not check for `balloc()` returning 0 (disk full) when allocating an *indirect* block; a full disk could corrupt block 0 instead of failing the write.
- **Task 2B:** `open()` follows links but there is no `O_NOFOLLOW`, so a link itself cannot be opened or inspected. Targets are resolved relative to the process's current directory. The return values of `argstr()` in `sys_symlink()` are not checked.
- **Task 3:** `mmap` does not verify that `fd` refers to a regular file (a pipe or console would break write-back). `sbrk` does not check for collisions with mapped regions. `exec` unmaps mappings *before* knowing whether the exec will succeed.

## Project Environment

- **Operating system:** xv6-riscv
- **Architecture:** RISC-V (rv64gc)
- **Languages:** C, RISC-V assembly, Perl (stub generator)
- **Build system:** Make
- **Emulator:** QEMU (`qemu-system-riscv64`)
- **Course:** CS-303 Operating Systems