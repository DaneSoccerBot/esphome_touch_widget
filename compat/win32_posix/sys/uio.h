/* POSIX sys/uio.h shim for Windows. */
#pragma once
#ifndef _COMPAT_SYS_UIO_H
#define _COMPAT_SYS_UIO_H

#include <stddef.h>
#include <winsock2.h>

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

/*
 * readv/writev via Winsock WSARecv/WSASend.  The fd must be a socket.
 */
static inline ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    DWORD bytes = 0;
    DWORD flags = 0;
    int rc = WSARecv((SOCKET)fd, (LPWSABUF)iov, (DWORD)iovcnt, &bytes, &flags, NULL, NULL);
    return rc == 0 ? (ssize_t)bytes : -1;
}

static inline ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    DWORD bytes = 0;
    int rc = WSASend((SOCKET)fd, (LPWSABUF)iov, (DWORD)iovcnt, &bytes, 0, NULL, NULL);
    return rc == 0 ? (ssize_t)bytes : -1;
}

#endif /* _COMPAT_SYS_UIO_H */
