/* TEST HARNESS ONLY (not part of the game): newlib low-level syscalls implemented with Linux
   syscalls, so an sh-elf program runs under qemu-sh4 linux-user (tests/sh4zam/README.md). */
#include <sys/stat.h>
#include <errno.h>
#include <stddef.h>
extern long _linux_syscall(long n, long a, long b, long c);
enum { SYS_exit = 1, SYS_read = 3, SYS_write = 4, SYS_open = 5, SYS_close = 6, SYS_lseek = 19, SYS_exit_group = 252 };
static long ret(long r) { if (r < 0 && r > -4096) { errno = (int)-r; return -1; } return r; }
int _write(int fd, const void* b, size_t n) { return (int)ret(_linux_syscall(SYS_write, fd, (long)b, (long)n)); }
int _read(int fd, void* b, size_t n) { return (int)ret(_linux_syscall(SYS_read, fd, (long)b, (long)n)); }
int _open(const char* p, int fl, int mode) {
    /* newlib O_* (sys/_default_fcntl.h) -> Linux: RDONLY/WRONLY/RDWR same; CREAT 0x200->0x40, TRUNC 0x400->0x200, APPEND 0x8->0x400 */
    int lf = fl & 3; if (fl & 0x200) lf |= 0x40; if (fl & 0x400) lf |= 0x200; if (fl & 0x8) lf |= 0x400;
    return (int)ret(_linux_syscall(SYS_open, (long)p, lf, mode));
}
int _close(int fd) { return (int)ret(_linux_syscall(SYS_close, fd, 0, 0)); }
long _lseek(int fd, long off, int wh) { return ret(_linux_syscall(SYS_lseek, fd, off, wh)); }
int _fstat(int fd, struct stat* st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { return fd <= 2; }
void _exit(int code) { _linux_syscall(SYS_exit_group, code, 0, 0); for (;;) {} }
static char heap[64 << 20] __attribute__((aligned(32)));
static size_t heap_used;
void* _sbrk(ptrdiff_t incr) { if (heap_used + incr > sizeof heap) { errno = ENOMEM; return (void*)-1; } void* p = heap + heap_used; heap_used += incr; return p; }
int _kill(int p, int s) { (void)p; (void)s; return -1; }
int _getpid(void) { return 1; }
int _gettimeofday(void* tv, void* tz) { (void)tv; (void)tz; return -1; }
int _times(void* t) { (void)t; return -1; }
int _unlink(const char* p) { (void)p; return -1; }
int _link(const char* a, const char* b) { (void)a; (void)b; return -1; }
int _stat(const char* p, struct stat* st) { (void)p; (void)st; return -1; }


