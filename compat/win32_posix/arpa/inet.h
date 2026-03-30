/* POSIX arpa/inet.h shim for Windows (Winsock2). */
#pragma once
#ifndef _COMPAT_ARPA_INET_H
#define _COMPAT_ARPA_INET_H

#include <winsock2.h>
#include <ws2tcpip.h>

/*
 * Winsock2 does not provide inet_aton().  ESPHome's ip_address.h uses it
 * via the ipaddr_aton macro.  Provide a thin wrapper around inet_pton().
 */
#ifndef inet_aton
static inline int inet_aton(const char *cp, struct in_addr *inp) {
    return inet_pton(AF_INET, cp, inp) == 1;
}
#endif

#endif /* _COMPAT_ARPA_INET_H */
