/* POSIX sys/ioctl.h shim for Windows (Winsock2). */
#pragma once
#ifndef _COMPAT_SYS_IOCTL_H
#define _COMPAT_SYS_IOCTL_H

#include <winsock2.h>
#include <stdarg.h>

/* Map POSIX ioctl() to Winsock ioctlsocket() for socket descriptors. */
#ifndef ioctl
#define ioctl ioctlsocket
#endif

/*
 * POSIX fcntl() constants and minimal implementation.
 * ESPHome uses fcntl(fd, F_GETFL/F_SETFL, O_NONBLOCK) for non-blocking
 * sockets. On Windows, this maps to ioctlsocket(FIONBIO).
 */
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif

static inline int _compat_fcntl(int fd, int cmd, ...) {
    if (cmd == F_GETFL) {
        return 0;
    }
    if (cmd == F_SETFL) {
        va_list ap;
        va_start(ap, cmd);
        int flags = va_arg(ap, int);
        va_end(ap);
        u_long mode = (flags & O_NONBLOCK) ? 1 : 0;
        return ioctlsocket((SOCKET)fd, FIONBIO, &mode) == 0 ? 0 : -1;
    }
    return -1;
}

/* Override the system fcntl (which doesn't exist on Windows anyway). */
#define fcntl _compat_fcntl

#endif /* _COMPAT_SYS_IOCTL_H */
