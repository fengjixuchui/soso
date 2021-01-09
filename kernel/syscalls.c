//TODO: seperate files

#include "syscalls.h"
#include "fs.h"
#include "process.h"
#include "alloc.h"
#include "pipe.h"
#include "debugprint.h"
#include "timer.h"
#include "sleep.h"
#include "ttydriver.h"
#include "spinlock.h"
#include "message.h"
#include "commonuser.h"
#include "syscalltable.h"
#include "isr.h"
#include "sharedmemory.h"
#include "serial.h"
#include "vmm.h"
#include "list.h"
#include "ttydev.h"
#include "syscall_select.h"
#include "errno.h"

struct iovec {
               void  *iov_base;    /* Starting address */
               size_t iov_len;     /* Number of bytes to transfer */
           };

struct statx {
    uint32 stx_mask;
    uint32 stx_blksize;
    uint64 stx_attributes;
    uint32 stx_nlink;
    uint32 stx_uid;
    uint32 stx_gid;
    uint16 stx_mode;
    uint16 pad1;
    uint64 stx_ino;
    uint64 stx_size;
    uint64 stx_blocks;
    uint64 stx_attributes_mask;
    struct {
        int64 tv_sec;
        uint32 tv_nsec;
        int32 pad;
    } stx_atime, stx_btime, stx_ctime, stx_mtime;
    uint32 stx_rdev_major;
    uint32 stx_rdev_minor;
    uint32 stx_dev_major;
    uint32 stx_dev_minor;
    uint64 spare[14];
};

struct k_sigaction {
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	unsigned mask[2];
};

/**************
 * All of syscall entered with interrupts disabled!
 * A syscall can enable interrupts if it is needed.
 *
 **************/

static void handleSyscall(Registers* regs);

static void* gSyscallTable[SYSCALL_COUNT];

struct rusage;


int syscall_open(const char *pathname, int flags);
int syscall_close(int fd);
int syscall_read(int fd, void *buf, int nbytes);
int syscall_write(int fd, void *buf, int nbytes);
int syscall_lseek(int fd, int offset, int whence);
int syscall_stat(const char *path, struct stat *buf);
int syscall_fstat(int fd, struct stat *buf);
int syscall_ioctl(int fd, int32 request, void *arg);
int syscall_exit();
void* syscall_sbrk(uint32 increment);
int syscall_fork();
int syscall_getpid();
int syscall_execute(const char *path, char *const argv[], char *const envp[]);
int syscall_execve(const char *path, char *const argv[], char *const envp[]);
int syscall_wait(int *wstatus);
int syscall_kill(int pid, int sig);
int syscall_mount(const char *source, const char *target, const char *fsType, unsigned long flags, void *data);
int syscall_unmount(const char *target);
int syscall_mkdir(const char *path, uint32 mode);
int syscall_rmdir(const char *path);
int syscall_getdents(int fd, char *buf, int nbytes);
int syscall_getWorkingDirectory(char *buf, int size);
int syscall_setWorkingDirectory(const char *path);
int syscall_managePipe(const char *pipeName, int operation, int data);
int syscall_readDir(int fd, void *dirent, int index);
int syscall_getUptimeMilliseconds();
int syscall_sleepMilliseconds(int ms);
int syscall_executeOnTTY(const char *path, char *const argv[], char *const envp[], const char *ttyPath);
int syscall_manageMessage(int command, void* message);
int syscall_rt_sigaction(int signum, const struct k_sigaction *act, struct k_sigaction *oldact, uint32 sigsetsize);
void* syscall_mmap(void *addr, int length, int flags, int prot, int fd, int offset);
int syscall_munmap(void *addr, int length);
int syscall_shm_open(const char *name, int oflag, int mode);
int syscall_shm_unlink(const char *name);
int syscall_ftruncate(int fd, int size);
int syscall_posix_openpt(int flags);
int syscall_ptsname_r(int fd, char *buf, int buflen);
int syscall_printk(const char *str, int num);
int syscall_readv(int fd, const struct iovec *iovs, int iovcnt);
int syscall_writev(int fd, const struct iovec *iovs, int iovcnt);
int syscall_set_thread_area(void *p);
int syscall_set_tid_address(void* p);
int syscall_exit_group(int status);
int syscall_llseek(unsigned int fd, unsigned int offset_high,
            unsigned int offset_low, int64 *result,
            unsigned int whence);

int syscall_statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf);
int syscall_wait4(int pid, int *wstatus, int options, struct rusage *rusage);
int32 syscall_clock_gettime64(int32 clockid, struct timespec *tp);
int32 syscall_clock_settime64(int32 clockid, const struct timespec *tp);
int32 syscall_clock_getres64(int32 clockid, struct timespec *res);

void initialiseSyscalls()
{
    memset((uint8*)gSyscallTable, 0, sizeof(void*) * SYSCALL_COUNT);

    gSyscallTable[SYS_open] = syscall_open;
    gSyscallTable[SYS_close] = syscall_close;
    gSyscallTable[SYS_read] = syscall_read;
    gSyscallTable[SYS_write] = syscall_write;
    gSyscallTable[SYS_lseek] = syscall_lseek;
    gSyscallTable[SYS_stat] = syscall_stat;
    gSyscallTable[SYS_fstat] = syscall_fstat;
    gSyscallTable[SYS_ioctl] = syscall_ioctl;
    gSyscallTable[SYS_exit] = syscall_exit;
    gSyscallTable[SYS_sbrk] = syscall_sbrk;
    gSyscallTable[SYS_fork] = syscall_fork;
    gSyscallTable[SYS_getpid] = syscall_getpid;

    gSyscallTable[SYS_execute] = syscall_execute;
    gSyscallTable[SYS_execve] = syscall_execve;
    gSyscallTable[SYS_wait] = syscall_wait;
    gSyscallTable[SYS_kill] = syscall_kill;
    gSyscallTable[SYS_mount] = syscall_mount;
    gSyscallTable[SYS_unmount] = syscall_unmount;
    gSyscallTable[SYS_mkdir] = syscall_mkdir;
    gSyscallTable[SYS_rmdir] = syscall_rmdir;
    gSyscallTable[SYS_getdents] = syscall_getdents;
    gSyscallTable[SYS_getWorkingDirectory] = syscall_getWorkingDirectory;
    gSyscallTable[SYS_setWorkingDirectory] = syscall_setWorkingDirectory;
    gSyscallTable[SYS_managePipe] = syscall_managePipe;
    gSyscallTable[SYS_readDir] = syscall_readDir;
    gSyscallTable[SYS_getUptimeMilliseconds] = syscall_getUptimeMilliseconds;
    gSyscallTable[SYS_sleepMilliseconds] = syscall_sleepMilliseconds;
    gSyscallTable[SYS_executeOnTTY] = syscall_executeOnTTY;
    gSyscallTable[SYS_manageMessage] = syscall_manageMessage;
    gSyscallTable[SYS_rt_sigaction] = syscall_rt_sigaction;
    gSyscallTable[SYS_mmap] = syscall_mmap;
    gSyscallTable[SYS_munmap] = syscall_munmap;
    gSyscallTable[SYS_shm_open] = syscall_shm_open;
    gSyscallTable[SYS_shm_unlink] = syscall_shm_unlink;
    gSyscallTable[SYS_ftruncate] = syscall_ftruncate;
    gSyscallTable[SYS_posix_openpt] = syscall_posix_openpt;
    gSyscallTable[SYS_ptsname_r] = syscall_ptsname_r;
    gSyscallTable[SYS_printk] = syscall_printk;
    gSyscallTable[SYS_readv] = syscall_readv;
    gSyscallTable[SYS_writev] = syscall_writev;
    gSyscallTable[SYS_set_thread_area] = syscall_set_thread_area;
    gSyscallTable[SYS_set_tid_address] = syscall_set_tid_address;
    gSyscallTable[SYS_exit_group] = syscall_exit_group;
    gSyscallTable[SYS_llseek] = syscall_llseek;
    gSyscallTable[SYS_select] = syscall_select;
    gSyscallTable[SYS_statx] = syscall_statx;
    gSyscallTable[SYS_wait4] = syscall_wait4;
    gSyscallTable[SYS_clock_gettime64] = syscall_clock_gettime64;
    gSyscallTable[SYS_clock_settime64] = syscall_clock_settime64;
    gSyscallTable[SYS_clock_getres64] = syscall_clock_getres64;

    // Register our syscall handler.
    registerInterruptHandler (0x80, &handleSyscall);
}

static void handleSyscall(Registers* regs)
{
    Thread* thread = getCurrentThread();
    Process* process = thread->owner;

    ++thread->calledSyscallCount;

    if (regs->eax >= SYSCALL_COUNT)
    {
        printkf("Unknown SYSCALL:%d (pid:%d)\n", regs->eax, process->pid);
        Debug_PrintF("Unknown SYSCALL:%d (pid:%d)\n", regs->eax, process->pid);

        regs->eax = -1;
        return;
    }

    void *location = gSyscallTable[regs->eax];

    if (NULL == location)
    {
        printkf("Unused SYSCALL:%d (pid:%d)\n", regs->eax, process->pid);
        Debug_PrintF("Unused SYSCALL:%d (pid:%d)\n", regs->eax, process->pid);

        regs->eax = -1;
        return;
    }

    //Screen_PrintF("We are in syscall_handler\n");
    //Screen_PrintInterruptsEnabled();

    //I think it is better to enable interrupts in syscall implementations if it is needed.

    int ret;
    asm volatile (" \
      pushl %1; \
      pushl %2; \
      pushl %3; \
      pushl %4; \
      pushl %5; \
      call *%6; \
      popl %%ebx; \
      popl %%ebx; \
      popl %%ebx; \
      popl %%ebx; \
      popl %%ebx; \
    " : "=a" (ret) : "r" (regs->edi), "r" (regs->esi), "r" (regs->edx), "r" (regs->ecx), "r" (regs->ebx), "r" (location));
    regs->eax = ret;
}

int syscall_printk(const char *str, int num)
{
    if (!checkUserAccess((char*)str))
    {
        return -EFAULT;
    }

    printkf(str, num);

    return 0;
}

int syscall_open(const char *pathname, int flags)
{
    if (!checkUserAccess((char*)pathname))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        FileSystemNode* node = getFileSystemNodeAbsoluteOrRelative(pathname, process);
        if (node)
        {
            File* file = open_fs(node, flags);

            if (file)
            {
                return file->fd;
            }
        }
    }

    return -1;
}

int syscall_close(int fd)
{
    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                close_fs(file);

                return 0;
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_read(int fd, void *buf, int nbytes)
{
    if (!checkUserAccess(buf))
    {
        return -EFAULT;
    }

    //Screen_PrintF("syscall_read: begin - nbytes:%d\n", nbytes);

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                //Debug_PrintF("syscall_read(%d): %s\n", process->pid, buf);

                //enableInterrupts();

                //Each handler is free to enable interrupts.
                //We don't enable them here.

                int ret = read_fs(file, nbytes, buf);

                return ret;
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_write(int fd, void *buf, int nbytes)
{
    if (!checkUserAccess(buf))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        //Screen_PrintF("syscall_write() called from process: %d. fd:%d\n", process->pid, fd);

        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                /*
                for (int i = 0; i < nbytes; ++i)
                {
                    Debug_PrintF("pid:%d syscall_write:buf[%d]=%c\n", process->pid, i, ((char*)buf)[i]);
                }
                */

                uint32 writeResult = write_fs(file, nbytes, buf);

                return writeResult;
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_readv(int fd, const struct iovec *iovs, int iovcnt)
{
    if (!checkUserAccess((void*)iovs))
    {
        return -EFAULT;
    }

    int result = 0;
    for (int i = 0; i < iovcnt; ++i)
    {
        const struct iovec *iov = iovs + i;

        if (iov->iov_len > 0)
        {
            int bytes = syscall_read(fd, iov->iov_base, iov->iov_len);

            if (bytes < 0)
            {
                return  bytes;
            }

            result += bytes;
        }
    }

    return result;
}

int syscall_writev(int fd, const struct iovec *iovs, int iovcnt)
{
    if (!checkUserAccess((void*)iovs))
    {
        return -EFAULT;
    }

    int result = 0;
    for (int i = 0; i < iovcnt; ++i)
    {
        const struct iovec *iov = iovs + i;

        if (iov->iov_len > 0)
        {
            int bytes = syscall_write(fd, iov->iov_base, iov->iov_len);

            if (bytes < 0)
            {
                return  bytes;
            }

            result += bytes;
        }
    }

    return result;
}

int syscall_set_thread_area(void *p)
{
    if (!checkUserAccess(p))
    {
        return -EFAULT;
    }

    return 0;
}

int syscall_set_tid_address(void* p)
{
    if (!checkUserAccess(p))
    {
        return -EFAULT;
    }

    return getCurrentThread()->threadId;
}

int syscall_exit_group(int status)
{
    return syscall_exit();
}

int syscall_lseek(int fd, int offset, int whence)
{
    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                int result = lseek_fs(file, offset, whence);

                return result;
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_llseek(unsigned int fd, unsigned int offset_high,
            unsigned int offset_low, int64 *result,
            unsigned int whence)
{
    if (!checkUserAccess(result))
    {
        return -EFAULT;
    }

    //this syscall is used for large files in 32 bit systems for the offset (offset_high<<32) | offset_low

    Process* process = getCurrentThread()->owner;
    //printkf("syscall_llseek() called from process: %d. fd:%d\n", process->pid, fd);

    if (offset_high != 0)
    {
        return -1;
    }

    int res = syscall_lseek(fd, offset_low, whence);

    if (res < 0)
    {
        return res;
    }

    *result = res;

    return  0;
}

int syscall_stat(const char *path, struct stat *buf)
{
    if (!checkUserAccess((char*)path))
    {
        return -EFAULT;
    }

    if (!checkUserAccess(buf))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        FileSystemNode* node = getFileSystemNodeAbsoluteOrRelative(path, process);

        if (node)
        {
            return stat_fs(node, buf);
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_fstat(int fd, struct stat *buf)
{
    if (!checkUserAccess(buf))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                return stat_fs(file->node, buf);
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_ioctl(int fd, int32 request, void *arg)
{
    //Important!!
    //We don't checkUserAccess() for arg here. Because it is not always a pointer.
    //So it is driver's responsibility to checkUserAccess(arg) for using it as a pointer.

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        //Serial_PrintF("syscall_ioctl fd:%d request:%d(%x) arg:%d(%x) pid:%d\n", fd, request, request, arg, arg, process->pid);

        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                return ioctl_fs(file, request, arg);
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_exit()
{
    Thread* thread = getCurrentThread();

    signalThread(thread, SIGTERM);
    
    waitForSchedule();

    return -1;
}

void* syscall_sbrk(uint32 increment)
{
    //Screen_PrintF("syscall_sbrk() !!! inc:%d\n", increment);

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        return sbrk(process, increment);
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return (void*)-1;
}

int syscall_fork()
{
    //Not implemented

    return -1;
}

int syscall_getpid()
{
    Process* process = getCurrentThread()->owner;
    if (process)
    {
        return process->pid;
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

//NON-posix
int syscall_execute(const char *path, char *const argv[], char *const envp[])
{
    if (!checkUserAccess((char*)path))
    {
        return -EFAULT;
    }

    if (!checkUserAccessStringArray(argv))
    {
        return -EFAULT;
    }

    if (!checkUserAccessStringArray(envp))
    {
        return -EFAULT;
    }

    int result = -1;

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        FileSystemNode* node = getFileSystemNodeAbsoluteOrRelative(path, process);
        if (node)
        {
            File* f = open_fs(node, 0);
            if (f)
            {
                void* image = kmalloc(node->length);

                //Screen_PrintF("executing %s and its %d bytes\n", filename, node->length);

                int32 bytesRead = read_fs(f, node->length, image);

                //Screen_PrintF("syscall_execute: read_fs returned %d bytes\n", bytesRead);

                if (bytesRead > 0)
                {
                    char* name = "UserProcess";
                    if (NULL != argv)
                    {
                        name = argv[0];
                    }
                    Process* newProcess = createUserProcessFromElfData(name, image, argv, envp, process, NULL);

                    if (newProcess)
                    {
                        result = newProcess->pid;
                    }
                }
                close_fs(f);

                kfree(image);
            }

        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return result;
}

int syscall_executeOnTTY(const char *path, char *const argv[], char *const envp[], const char *ttyPath)
{
    if (!checkUserAccess((char*)path))
    {
        return -EFAULT;
    }

    if (!checkUserAccessStringArray(argv))
    {
        return -EFAULT;
    }

    if (!checkUserAccessStringArray(envp))
    {
        return -EFAULT;
    }

    if (!checkUserAccess((char*)ttyPath))
    {
        return -EFAULT;
    }

    int result = -1;

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        FileSystemNode* node = getFileSystemNodeAbsoluteOrRelative(path, process);
        FileSystemNode* ttyNode = getFileSystemNodeAbsoluteOrRelative(ttyPath, process);
        if (node && ttyNode)
        {
            File* f = open_fs(node, 0);
            if (f)
            {
                void* image = kmalloc(node->length);

                //Screen_PrintF("executing %s and its %d bytes\n", filename, node->length);

                int32 bytesRead = read_fs(f, node->length, image);

                //Screen_PrintF("syscall_execute: read_fs returned %d bytes\n", bytesRead);

                if (bytesRead > 0)
                {
                    char* name = "UserProcess";
                    if (NULL != argv)
                    {
                        name = argv[0];
                    }
                    Process* newProcess = createUserProcessFromElfData(name, image, argv, envp, process, ttyNode);

                    if (newProcess)
                    {
                        result = newProcess->pid;
                    }
                }
                close_fs(f);

                kfree(image);
            }

        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return result;
}

int syscall_execve(const char *path, char *const argv[], char *const envp[])
{
    if (!checkUserAccess((char*)path))
    {
        return -EFAULT;
    }

    if (!checkUserAccessStringArray(argv))
    {
        return -EFAULT;
    }

    if (!checkUserAccessStringArray(envp))
    {
        return -EFAULT;
    }

    Process* callingProcess = getCurrentThread()->owner;

    FileSystemNode* node = getFileSystemNode(path);
    if (node)
    {
        File* f = open_fs(node, 0);
        if (f)
        {
            void* image = kmalloc(node->length);

            if (read_fs(f, node->length, image) > 0)
            {
                disableInterrupts(); //just in case if a file operation left interrupts enabled.

                Process* newProcess = createUserProcessEx("fromExecve", callingProcess->pid, 0, NULL, image, argv, envp, NULL, callingProcess->tty);

                close_fs(f);

                kfree(image);

                if (newProcess)
                {
                    destroyProcess(callingProcess);

                    waitForSchedule();

                    //unreachable
                }
            }

            close_fs(f);

            kfree(image);
        }
    }


    //if it all goes well, this line is unreachable
    return 0;
}

int syscall_wait(int *wstatus)
{
    if (!checkUserAccess(wstatus))
    {
        return -EFAULT;
    }

    //TODO: return pid of exited child. implement with sendsignal

    Thread* currentThread = getCurrentThread();

    Process* process = currentThread->owner;
    if (process)
    {
        Thread* thread = getMainKernelThread();
        while (thread)
        {
            if (process == thread->owner->parent)
            {
                //We have a child process

                changeThreadState(currentThread, TS_WAITCHILD, NULL);

                enableInterrupts();
                while (currentThread->state == TS_WAITCHILD);

                break;
            }

            thread = thread->next;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_wait4(int pid, int *wstatus, int options, struct rusage *rusage)
{
    if (!checkUserAccess(wstatus))
    {
        return -EFAULT;
    }

    if (!checkUserAccess(rusage))
    {
        return -EFAULT;
    }

    Thread* currentThread = getCurrentThread();

    Process* process = currentThread->owner;
    if (process)
    {
        Thread* thread = getMainKernelThread();
        while (thread)
        {
            if (process == thread->owner->parent)
            {
                if (pid < 0 || pid == (int)thread->owner->pid)
                {
                    changeThreadState(currentThread, TS_WAITCHILD, NULL);

                    enableInterrupts();
                    while (currentThread->state == TS_WAITCHILD);

                    break;
                }
            }

            thread = thread->next;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int32 syscall_clock_gettime64(int32 clockid, struct timespec *tp)
{
    if (!checkUserAccess(tp))
    {
        return -EFAULT;
    }

    return clock_gettime64(clockid, tp);
}

int32 syscall_clock_settime64(int32 clockid, const struct timespec *tp)
{
    if (!checkUserAccess((void*)tp))
    {
        return -EFAULT;
    }

    return clock_settime64(clockid, tp);
}

int32 syscall_clock_getres64(int32 clockid, struct timespec *res)
{
    if (!checkUserAccess(res))
    {
        return -EFAULT;
    }

    return clock_getres64(clockid, res);
}

int syscall_kill(int pid, int sig)
{
    Process* selfProcess = getCurrentThread()->owner;

    Thread* thread = getMainKernelThread();
    while (thread)
    {
        if (pid == thread->owner->pid)
        {
            //We have found the process

            Process* threadOwnerProcess = thread->owner;

            signalThread(thread, SIGKILL);

            if (threadOwnerProcess == selfProcess)
            {
                waitForSchedule();
            }
            else
            {
                return 0;
            }

            break;
        }

        thread = thread->next;
    }

    return -1;
}

int syscall_mount(const char *source, const char *target, const char *fsType, unsigned long flags, void *data)//non-posix
{
    if (!checkUserAccess((char*)source))
    {
        return -EFAULT;
    }

    if (!checkUserAccess((char*)target))
    {
        return -EFAULT;
    }

    if (!checkUserAccess((char*)fsType))
    {
        return -EFAULT;
    }

    if (!checkUserAccess(data))
    {
        return -EFAULT;
    }

    BOOL result = mountFileSystem(source, target, fsType, flags, data);

    if (TRUE == result)
    {
        return 0;//on success
    }

    return -1;//on error
}

int syscall_unmount(const char *target)//non-posix
{
    if (!checkUserAccess((char*)target))
    {
        return -EFAULT;
    }

    FileSystemNode* targetNode = getFileSystemNode(target);

    if (targetNode)
    {
        if (targetNode->nodeType == FT_MountPoint)
        {
            targetNode->nodeType = FT_Directory;

            targetNode->mountPoint = NULL;

            //TODO: check conditions, maybe busy. make clean up.

            return 0;//on success
        }
    }

    return -1;//on error
}

int syscall_mkdir(const char *path, uint32 mode)
{
    if (!checkUserAccess((char*)path))
    {
        return -EFAULT;
    }

    char parentPath[128];
    const char* name = NULL;
    int length = strlen(path);
    for (int i = length - 1; i >= 0; --i)
    {
        if (path[i] == '/')
        {
            name = path + i + 1;
            strncpy(parentPath, path, i);
            parentPath[i] = '\0';
            break;
        }
    }

    if (strlen(parentPath) == 0)
    {
        parentPath[0] = '/';
        parentPath[1] = '\0';
    }

    if (strlen(name) == 0)
    {
        return -1;
    }

    //Screen_PrintF("mkdir: parent:[%s] name:[%s]\n", parentPath, name);

    FileSystemNode* targetNode = getFileSystemNode(parentPath);

    if (targetNode)
    {
        BOOL success = mkdir_fs(targetNode, name, mode);
        if (success)
        {
            return 0;//on success
        }
    }

    return -1;//on error
}

int syscall_rmdir(const char *path)
{
    if (!checkUserAccess((char*)path))
    {
        return -EFAULT;
    }

    //TODO:
    //return 0;//on success

    return -1;//on error
}

int syscall_getdents(int fd, char *buf, int nbytes)
{
    if (!checkUserAccess(buf))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                //Screen_PrintF("syscall_getdents(%d): %s\n", process->pid, buf);

                int byteCounter = 0;

                int index = 0;
                FileSystemDirent* dirent = readdir_fs(file->node, index);

                while (NULL != dirent && (byteCounter + sizeof(FileSystemDirent) <= nbytes))
                {
                    memcpy((uint8*)buf + byteCounter, (uint8*)dirent, sizeof(FileSystemDirent));

                    byteCounter += sizeof(FileSystemDirent);

                    index += 1;
                    dirent = readdir_fs(file->node, index);
                }

                return byteCounter;
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;//on error
}

int syscall_readDir(int fd, void *dirent, int index)
{
    if (!checkUserAccess(dirent))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                FileSystemDirent* direntFs = readdir_fs(file->node, index);

                if (direntFs)
                {
                    memcpy((uint8*)dirent, (uint8*)direntFs, sizeof(FileSystemDirent));

                    return 1;
                }
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;//on error
}

int syscall_getWorkingDirectory(char *buf, int size)
{
    if (!checkUserAccess(buf))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (process->workingDirectory)
        {
            return getFileSystemNodePath(process->workingDirectory, buf, size);
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;//on error
}

int syscall_setWorkingDirectory(const char *path)
{
    if (!checkUserAccess((char*)path))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        FileSystemNode* node = getFileSystemNodeAbsoluteOrRelative(path, process);

        if (node)
        {
            process->workingDirectory = node;

            return 0; //success
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;//on error
}

int syscall_managePipe(const char *pipeName, int operation, int data)
{
    if (!checkUserAccess((char*)pipeName))
    {
        return -EFAULT;
    }

    int result = -1;

    switch (operation)
    {
    case 0:
        result = existsPipe(pipeName);
        break;
    case 1:
        result = createPipe(pipeName, data);
        break;
    case 2:
        result = destroyPipe(pipeName);
        break;
    }

    return result;
}

int syscall_getUptimeMilliseconds()
{
    return getUptimeMilliseconds();
}

int syscall_sleepMilliseconds(int ms)
{
    Thread* thread = getCurrentThread();

    sleepMilliseconds(thread, (uint32)ms);

    return 0;
}

int syscall_manageMessage(int command, void* message)
{
    if (!checkUserAccess(message))
    {
        return -EFAULT;
    }

    Thread* thread = getCurrentThread();

    int result = -1;

    switch (command)
    {
    case 0:
        result = getMessageQueueCount(thread);
        break;
    case 1:
        sendMesage(thread, (SosoMessage*)message);
        result = 0;
        break;
    case 2:
        //make blocking
        result = getNextMessage(thread, (SosoMessage*)message);
        break;
    default:
        break;
    }

    return result;
}

int syscall_rt_sigaction(int signum, const struct k_sigaction *act, struct k_sigaction *oldact, uint32 sigsetsize)
{
    //TODO
    return -1;
}

void* syscall_mmap(void *addr, int length, int flags, int prot, int fd, int offset)
{
    uint32 vAddressHint = (uint32)addr;

    if (vAddressHint < USER_OFFSET)
    {
        vAddressHint = USER_MMAP_START;
    }

    if (length <= 0)
    {
        return (void*)-1;
    }

    Process* process = getCurrentThread()->owner;

    if (process)
    {
        if (fd < 0)
        {
            int neededPages = PAGE_COUNT(length);
            uint32 freePages = getFreePageCount();
            //printkf("alloc from mmap length:%x neededPages:%d freePages:%d\n", length, neededPages, freePages);
            if ((uint32)neededPages + 1 > freePages)
            {
                return (void*)-1;
            }
            uint32* physicalArray = (uint32*)kmalloc(neededPages * sizeof(uint32));
            for (int i = 0; i < neededPages; ++i)
            {
                uint32 pageFrame = acquirePageFrame4K();
                physicalArray[i] = pageFrame;
            }

            void* mem = mapMemory(process, vAddressHint, physicalArray, neededPages, TRUE);
            if (mem != NULL)
            {
                memset((uint8*)mem, 0, length);
            }
            else
            {
                for (int i = 0; i < neededPages; ++i)
                {
                    releasePageFrame4K(physicalArray[i]);
                }

                mem = (void*)-1;
            }
            kfree(physicalArray);

            return mem;
        }
        else
        {
            if (fd < MAX_OPENED_FILES)
            {
                File* file = process->fd[fd];

                if (file)
                {
                    void* ret = mmap_fs(file, length, offset, flags);

                    if (ret)
                    {
                        return ret;
                    }
                }
                else
                {
                    return (void*)-EBADF;
                }
            }
            else
            {
                return (void*)-EBADF;
            }
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return (void*)-1;
}

int syscall_munmap(void *addr, int length)
{
    if (!checkUserAccess(addr))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;

    if (process)
    {
        int fd = -1;
        if (fd < 0)
        {
            if ((uint32)addr < USER_OFFSET)
            {
                return -1;
            }

            if (TRUE == unmapMemory(process, (uint32)addr, PAGE_COUNT(length)))
            {
                return 0;
            }
            return -1;
        }
        else
        {
            if (fd < MAX_OPENED_FILES)
            {
                File* file = process->fd[fd];

                if (file)
                {
                    if (munmap_fs(file, addr, length))
                    {
                        return 0;//on success
                    }
                }
                else
                {
                    return -EBADF;
                }
            }
            else
            {
                return -EBADF;
            }
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_SYMLINK_FOLLOW 0x400
#define AT_EACCESS 0x200
#define AT_NO_AUTOMOUNT 0x800
#define AT_EMPTY_PATH 0x1000
#define AT_STATX_SYNC_TYPE 0x6000
#define AT_STATX_SYNC_AS_STAT 0x0000
#define AT_STATX_FORCE_SYNC 0x2000
#define AT_STATX_DONT_SYNC 0x4000
#define AT_RECURSIVE 0x8000

int syscall_statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
{
    if (!checkUserAccess((char*)pathname))
    {
        return -EFAULT;
    }

    if (!checkUserAccess(statxbuf))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        int pathLen = strlen(pathname);

        FileSystemNode* node = NULL;

        if (pathLen > 0)
        {
            if (pathname[0] == '/') //ignore dirfd. this is absolute path
            {
                node = getFileSystemNode(pathname);
            }
            else
            {
                if (dirfd == AT_FDCWD) //pathname is relative to Current Working Directory
                {
                    node = getFileSystemNodeRelativeToNode(pathname, process->workingDirectory);
                }
                else if (dirfd >= 0 && dirfd < MAX_OPENED_FILES)
                {
                    File* dirFdDir = process->fd[dirfd];
                    if ((dirFdDir->node->nodeType & FT_Directory) == FT_Directory) //pathname is relative to the directory that dirfd refers to
                    {
                        node = getFileSystemNodeRelativeToNode(pathname, dirFdDir->node);
                    }
                }
            }
        }
        else if (pathLen == 0)
        {
            if ((flags & AT_EMPTY_PATH) == AT_EMPTY_PATH)
            {
                if (dirfd >= 0 && dirfd < MAX_OPENED_FILES)
                {
                    node = process->fd[dirfd]->node;
                }
            }
        }

        if (node)
        {
            struct stat st;
            memset((uint8*)&st, 0, sizeof(st));

            int statResult = stat_fs(node, &st);

            statxbuf->stx_mode = st.st_mode;
            statxbuf->stx_size = st.st_size;

            return statResult;
        }


    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

#define O_CREAT 0x200

int syscall_shm_open(const char *name, int oflag, int mode)
{
    if (!checkUserAccess((char*)name))
    {
        return -EFAULT;
    }

    FileSystemNode* node = NULL;

    if ((oflag & O_CREAT) == O_CREAT)
    {
        node = createSharedMemory(name);
    }
    else
    {
        node = getSharedMemoryNode(name);
    }

    if (node)
    {
        File* file = open_fs(node, oflag);

        if (file)
        {
            return file->fd;
        }
    }

    return -1;
}

int syscall_shm_unlink(const char *name)
{
    if (!checkUserAccess((char*)name))
    {
        return -EFAULT;
    }

    return -1;
}

int syscall_ftruncate(int fd, int size)
{
    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                return ftruncate_fs(file, size);
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_posix_openpt(int flags)
{
    Process* process = getCurrentThread()->owner;
    if (process)
    {
        FileSystemNode* node = createTTYDev();
        if (node)
        {
            TtyDev* ttyDev = (TtyDev*)node->privateNodeData;
            ttyDev->controllingProcess = process->pid;
            ttyDev->foregroundProcess = process->pid;

            File* file = open_fs(node, flags);

            if (file)
            {
                return file->fd;
            }
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;
}

int syscall_ptsname_r(int fd, char *buf, int buflen)
{
    if (!checkUserAccess(buf))
    {
        return -EFAULT;
    }

    Process* process = getCurrentThread()->owner;
    if (process)
    {
        if (fd < MAX_OPENED_FILES)
        {
            File* file = process->fd[fd];

            if (file)
            {
                TtyDev* ttyDev = file->node->privateNodeData;

                FileSystemNode* slaveNode = ttyDev->slaveNode;

                int result = getFileSystemNodePath(slaveNode, buf, buflen);

                if (result >= 0)
                {
                    return 0;
                }
            }
            else
            {
                return -EBADF;
            }
        }
        else
        {
            return -EBADF;
        }
    }
    else
    {
        PANIC("Process is NULL!\n");
    }

    return -1;//on error
}
