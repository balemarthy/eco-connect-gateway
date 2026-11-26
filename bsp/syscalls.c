/*
 * Minimal newlib system call stubs.
 *
 * newlib-nano needs these symbols when anything from stdio or exit() is linked.
 * The firmware does not use files, so every call reports failure. _sbrk hands out
 * RAM between the end of .bss and the reserved main stack; after step 03 nothing
 * should call malloc() (FreeRTOS uses its own heap_4 array).
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

int _close(int fd);
int _fstat(int fd, struct stat *st);
int _isatty(int fd);
int _lseek(int fd, int off, int whence);
int _read(int fd, char *buf, int len);
int _write(int fd, const char *buf, int len);
int _getpid(void);
int _kill(int pid, int sig);
void _exit(int status);
void *_sbrk(ptrdiff_t incr);

int _close(int fd)                          { (void)fd; errno = EBADF; return -1; }
int _fstat(int fd, struct stat *st)         { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                         { (void)fd; return 1; }
int _lseek(int fd, int off, int whence)     { (void)fd; (void)off; (void)whence; return 0; }
int _read(int fd, char *buf, int len)       { (void)fd; (void)buf; (void)len; errno = EIO; return -1; }
int _getpid(void)                           { return 1; }
int _kill(int pid, int sig)                 { (void)pid; (void)sig; errno = EINVAL; return -1; }

/* Weak so the console driver can provide a real one later. */
__attribute__((weak)) int _write(int fd, const char *buf, int len)
{
    (void)fd;
    (void)buf;
    return len;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}

void *_sbrk(ptrdiff_t incr)
{
    extern uint8_t _end;              /* linker script: first byte after .bss */
    extern uint8_t _estack;           /* linker script: top of RAM */
    extern uint32_t _Min_Stack_Size;  /* linker script: reserved main stack */
    static uint8_t *brk = NULL;

    const uint8_t *limit = &_estack - (uint32_t)&_Min_Stack_Size;
    if (brk == NULL) {
        brk = &_end;
    }
    if (brk + incr > limit) {
        errno = ENOMEM;
        return (void *)-1;
    }
    uint8_t *prev = brk;
    brk += incr;
    return prev;
}
