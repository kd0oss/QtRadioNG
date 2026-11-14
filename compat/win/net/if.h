/*
 * net/if.h stand-in for Windows (mingw-w64 / MSYS2).
 *
 * Only the interface flag bits used with getifaddrs() are provided. Winsock
 * already defines IFF_UP, IFF_BROADCAST, IFF_LOOPBACK, IFF_POINTTOPOINT and
 * IFF_MULTICAST in ws2ipdef.h; IFF_RUNNING is added here.
 */
#ifndef QTR_WIN_NET_IF_H
#define QTR_WIN_NET_IF_H

#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef IFF_UP
#define IFF_UP           0x00000001
#endif
#ifndef IFF_BROADCAST
#define IFF_BROADCAST    0x00000002
#endif
#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK     0x00000004
#endif
#ifndef IFF_POINTTOPOINT
#define IFF_POINTTOPOINT 0x00000008
#endif
#ifndef IFF_MULTICAST
#define IFF_MULTICAST    0x00000010
#endif
#ifndef IFF_RUNNING
#define IFF_RUNNING      0x00000040
#endif

#ifndef IF_NAMESIZE
#define IF_NAMESIZE      64
#endif

#endif
