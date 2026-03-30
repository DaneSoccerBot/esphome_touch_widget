/* POSIX sys/socket.h shim for Windows (Winsock2). */
#pragma once
#ifndef _COMPAT_SYS_SOCKET_H
#define _COMPAT_SYS_SOCKET_H

#include <winsock2.h>
#include <ws2tcpip.h>

/* POSIX shutdown constants (already defined by Winsock as SD_*). */
#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

typedef int socklen_t;

/*
 * Ensure WSAStartup() is called before any Winsock function.
 * Uses a file-scoped C++ constructor so it runs before main().
 */
namespace _compat_wsa {
struct Init {
    Init() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
};
static Init _wsa_init;
}  // namespace _compat_wsa

#endif /* _COMPAT_SYS_SOCKET_H */
