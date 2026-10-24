#ifndef HOST_LINUX_H
#define HOST_LINUX_H

typedef unsigned int LinuxSize;
typedef long long LinuxOffset;

typedef struct LinuxStat {
    unsigned long long dev;
    unsigned int pad1;
    unsigned int ino32;
    unsigned int mode;
    unsigned int nlink;
    unsigned int uid;
    unsigned int gid;
    unsigned long long rdev;
    unsigned int pad2;
    long long size;
    unsigned int blksize;
    unsigned long long blocks;
    unsigned int atime;
    unsigned int atime_nsec;
    unsigned int mtime;
    unsigned int mtime_nsec;
    unsigned int ctime;
    unsigned int ctime_nsec;
    unsigned long long ino;
} __attribute__((packed)) LinuxStat;

typedef struct LinuxTimespec64 {
    long long sec;
    long long nsec;
} LinuxTimespec64;

#define LINUX_S_IFMT 0170000
#define LINUX_S_IFDIR 0040000
#define LINUX_S_IFREG 0100000
#define LINUX_S_IFLNK 0120000

#define LINUX_O_RDONLY 0
#define LINUX_O_WRONLY 1
#define LINUX_O_RDWR 2
#define LINUX_O_CREAT 0100
#define LINUX_O_EXCL 0200
#define LINUX_O_TRUNC 01000
#define LINUX_O_APPEND 02000
#define LINUX_O_DIRECTORY 0200000
#define LINUX_O_LARGEFILE 0100000
#define LINUX_O_CLOEXEC 02000000

#define LINUX_AT_FDCWD (-100)

#define LINUX_ENOENT 2
#define LINUX_EINTR 4
#define LINUX_EIO 5
#define LINUX_EBADF 9
#define LINUX_ENOMEM 12
#define LINUX_EACCES 13
#define LINUX_EEXIST 17
#define LINUX_ENOTDIR 20
#define LINUX_EISDIR 21
#define LINUX_EINVAL 22
#define LINUX_ENOSPC 28
#define LINUX_ESPIPE 29
#define LINUX_EPIPE 32
#define LINUX_ERANGE 34
#define LINUX_ENAMETOOLONG 36
#define LINUX_ENOSYS 38
#define LINUX_ENOTEMPTY 39

static inline long Linux_Call0(long number) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number) : "memory");
    return result;
}

static inline long Linux_Call1(long number, long a) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a) : "memory");
    return result;
}

static inline long Linux_Call2(long number, long a, long b) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a), "c"(b) : "memory");
    return result;
}

static inline long Linux_Call3(long number, long a, long b, long c) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}

static inline long Linux_Call4(long number, long a, long b, long c, long d) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "memory");
    return result;
}

static inline long Linux_Call5(long number, long a, long b, long c, long d, long e) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory");
    return result;
}

#define LINUX_SYS_exit_group 252
#define LINUX_SYS_read 3
#define LINUX_SYS_write 4
#define LINUX_SYS_open 5
#define LINUX_SYS_close 6
#define LINUX_SYS_unlink 10
#define LINUX_SYS_mkdir 39
#define LINUX_SYS_rmdir 40
#define LINUX_SYS_munmap 91
#define LINUX_SYS_ftruncate64 194
#define LINUX_SYS_stat64 195
#define LINUX_SYS_lstat64 196
#define LINUX_SYS_fstat64 197
#define LINUX_SYS_getcwd 183
#define LINUX_SYS_pread64 180
#define LINUX_SYS_pwrite64 181
#define LINUX_SYS_mmap 90
#define LINUX_SYS_getdents64 220
#define LINUX_SYS_readlink 85
#define LINUX_SYS__llseek 140
#define LINUX_SYS_utimensat 320
#define LINUX_SYS_clock_gettime64 403
#define LINUX_SYS_clock_gettime 265
#define LINUX_SYS_rt_sigaction 174

#define LINUX_PROT_READ 1
#define LINUX_PROT_WRITE 2
#define LINUX_MAP_PRIVATE 0x02
#define LINUX_MAP_ANONYMOUS 0x20
#define LINUX_MAP_NORESERVE 0x4000

static inline __attribute__((noreturn)) void Linux_Exit(int status) {
    for (;;) {
        Linux_Call1(LINUX_SYS_exit_group, status);
    }
}

static inline long Linux_Read(int fd, void* buffer, LinuxSize size) {
    return Linux_Call3(LINUX_SYS_read, fd, (long)buffer, size);
}

static inline long Linux_Write(int fd, const void* buffer, LinuxSize size) {
    return Linux_Call3(LINUX_SYS_write, fd, (long)buffer, size);
}

static inline long Linux_PRead(int fd, void* buffer, LinuxSize size, LinuxOffset offset) {
    return Linux_Call5(
        LINUX_SYS_pread64, fd, (long)buffer, size, (long)offset, (long)(offset >> 32)
    );
}

static inline long Linux_PWrite(int fd, const void* buffer, LinuxSize size, LinuxOffset offset) {
    return Linux_Call5(
        LINUX_SYS_pwrite64, fd, (long)buffer, size, (long)offset, (long)(offset >> 32)
    );
}

static inline long Linux_Open(const char* path, int flags, int mode) {
    return Linux_Call3(LINUX_SYS_open, (long)path, flags | LINUX_O_LARGEFILE, mode);
}

#define LINUX_SEEK_CUR 1
#define LINUX_SEEK_END 2

static inline long Linux_LSeek(int fd, LinuxOffset offset, int whence, LinuxOffset* result) {
    return Linux_Call5(
        LINUX_SYS__llseek, fd, (long)(offset >> 32), (long)offset, (long)result, whence
    );
}

static inline long Linux_Close(int fd) {
    return Linux_Call1(LINUX_SYS_close, fd);
}

static inline long Linux_Stat(const char* path, LinuxStat* st) {
    return Linux_Call2(LINUX_SYS_stat64, (long)path, (long)st);
}

static inline long Linux_LStat(const char* path, LinuxStat* st) {
    return Linux_Call2(LINUX_SYS_lstat64, (long)path, (long)st);
}

static inline long Linux_FStat(int fd, LinuxStat* st) {
    return Linux_Call2(LINUX_SYS_fstat64, fd, (long)st);
}

static inline long Linux_FTruncate(int fd, LinuxOffset length) {
    return Linux_Call3(LINUX_SYS_ftruncate64, fd, (long)length, (long)(length >> 32));
}

static inline long Linux_Unlink(const char* path) {
    return Linux_Call1(LINUX_SYS_unlink, (long)path);
}

static inline long Linux_Mkdir(const char* path, int mode) {
    return Linux_Call2(LINUX_SYS_mkdir, (long)path, mode);
}

static inline long Linux_Rmdir(const char* path) {
    return Linux_Call1(LINUX_SYS_rmdir, (long)path);
}

static inline long Linux_GetCwd(char* buffer, LinuxSize size) {
    return Linux_Call2(LINUX_SYS_getcwd, (long)buffer, size);
}

static inline long Linux_GetDents(int fd, void* buffer, LinuxSize size) {
    return Linux_Call3(LINUX_SYS_getdents64, fd, (long)buffer, size);
}

static inline long Linux_ReadLink(const char* path, char* buffer, LinuxSize size) {
    return Linux_Call3(LINUX_SYS_readlink, (long)path, (long)buffer, size);
}

static inline void* Linux_Map(LinuxSize size) {
    long arguments[6] = {
        0,
        (long)size,
        LINUX_PROT_READ | LINUX_PROT_WRITE,
        LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS | LINUX_MAP_NORESERVE,
        -1,
        0,
    };
    return (void*)Linux_Call1(LINUX_SYS_mmap, (long)arguments);
}

static inline int Linux_MapFailed(void* p) {
    return (unsigned long)p >= (unsigned long)-4095;
}

static inline long Linux_Unmap(void* p, LinuxSize size) {
    return Linux_Call2(LINUX_SYS_munmap, (long)p, size);
}

static inline long Linux_ClockGetTime(int clock, LinuxTimespec64* ts) {
    long result = Linux_Call2(LINUX_SYS_clock_gettime64, clock, (long)ts);
    if (result == -LINUX_ENOSYS) {
        long old[2];
        result = Linux_Call2(LINUX_SYS_clock_gettime, clock, (long)old);
        ts->sec = old[0];
        ts->nsec = old[1];
    }
    return result;
}

#endif
