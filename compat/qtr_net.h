/*
 * qtr_net.h - sockets for Linux and Windows (MSYS2, mingw-w64)
 *
 * Include this instead of <sys/socket.h>, <netinet/in.h>, <arpa/inet.h>,
 * <netdb.h> ...  On Linux it is those headers.  On Windows it is Winsock,
 * with the calls whose argument types differ wrapped so that the same code
 * compiles, plus a few helpers for what is done differently there:
 *
 *   qtr_net_init()            WSAStartup on Windows (call once, early)
 *   sock_close(s)             close() does not close a Winsock socket
 *   sock_error()              errno / WSAGetLastError()
 *   SOCK_EWOULDBLOCK ...      error codes to compare sock_error() with
 *   sock_set_nonblocking(s,on)
 *   sock_set_rcvtimeo(s,ms)   SO_RCVTIMEO: struct timeval / DWORD milliseconds
 *   sock_set_sndtimeo(s,ms)
 *   sock_set_rcvtimeo_us(s,us) microsecond form; Windows rounds up to 1 ms
 *                             (0 there would mean "wait forever")
 *   sock_set_reuseaddr(s)     POSIX SO_REUSEADDR; on Windows SO_EXCLUSIVEADDRUSE
 *                             (Windows SO_REUSEADDR would let two programs
 *                             share one port)
 *   sock_would_block(err)     true for "no data yet": EAGAIN/EWOULDBLOCK, and on
 *                             Windows also WSAETIMEDOUT (an SO_RCVTIMEO expiry)
 *   sock_connect_pending(err) true for a non-blocking connect still in progress
 *                             (EINPROGRESS; WSAEWOULDBLOCK on Windows)
 *   sock_wait_connected(s,ms) wait for a pending connect: 0 connected, -1 not
 *   sock_perror(msg)          perror() for socket calls (reports WSA errors)
 *   sock_readable(s,ms)       select() for one socket: 1 readable, 0 not, -1 error
 *
 * Sockets are kept in int, as in the rest of QtRadioNG: Winsock handles fit,
 * and INVALID_SOCKET is -1 there.
 */

#ifndef QTR_NET_H
#define QTR_NET_H

#include <stdio.h>
#include <string.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <unistd.h>
#include <errno.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0                  // no SIGPIPE on Windows
#endif
#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

// Winsock takes char * where POSIX takes void *
#define setsockopt(s, l, o, v, n)       setsockopt((s), (l), (o), (const char *)(v), (n))
#define getsockopt(s, l, o, v, n)       getsockopt((s), (l), (o), (char *)(v), (n))
#define recv(s, b, n, f)                recv((s), (char *)(b), (int)(n), (f))
#define send(s, b, n, f)                send((s), (const char *)(b), (int)(n), (f))
#define recvfrom(s, b, n, f, a, l)      recvfrom((s), (char *)(b), (int)(n), (f), (a), (l))
#define sendto(s, b, n, f, a, l)        sendto((s), (const char *)(b), (int)(n), (f), (a), (l))

#define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
#define SOCK_EAGAIN      WSAEWOULDBLOCK
#define SOCK_EINTR       WSAEINTR
#define SOCK_ETIMEDOUT   WSAETIMEDOUT
#define SOCK_EINPROGRESS WSAEWOULDBLOCK  // a non-blocking connect() in progress

static inline int qtr_net_init(void)
{
    static int done = 0;
    WSADATA wsa;

    if (done)
        return 0;
    done = 1;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline int sock_close(int s)
{
    return closesocket((SOCKET)s);
}

static inline int sock_error(void)
{
    return WSAGetLastError();
}

static inline int sock_set_nonblocking(int s, int on)
{
    u_long v = on ? 1 : 0;
    return ioctlsocket((SOCKET)s, FIONBIO, &v);
}

static inline int sock_set_rcvtimeo(int s, int ms)
{
    DWORD t = (DWORD)ms;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
}

static inline int sock_set_sndtimeo(int s, int ms)
{
    DWORD t = (DWORD)ms;
    return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
}

static inline int sock_set_reuseaddr(int s)
{
    BOOL on = TRUE;
    return setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, &on, sizeof(on));
}

static inline int sock_set_rcvtimeo_us(int s, long us)
{
    DWORD t = (DWORD)((us + 999) / 1000);
    if (t == 0 && us > 0)
        t = 1;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
}

#else   // POSIX

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define SOCK_EWOULDBLOCK EWOULDBLOCK
#define SOCK_EAGAIN      EAGAIN
#define SOCK_EINTR       EINTR
#define SOCK_ETIMEDOUT   ETIMEDOUT
#define SOCK_EINPROGRESS EINPROGRESS

static inline int qtr_net_init(void)
{
    return 0;
}

static inline int sock_close(int s)
{
    return close(s);
}

static inline int sock_error(void)
{
    return errno;
}

static inline int sock_set_nonblocking(int s, int on)
{
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0)
        return -1;
    return fcntl(s, F_SETFL, on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
}

static inline int sock_set_rcvtimeo(int s, int ms)
{
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

static inline int sock_set_sndtimeo(int s, int ms)
{
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static inline int sock_set_reuseaddr(int s)
{
    int on = 1;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
}

static inline int sock_set_rcvtimeo_us(int s, long us)
{
    struct timeval tv = { us / 1000000, us % 1000000 };
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

#endif

/* "No data yet" rather than a real error. */
static inline int sock_would_block(int err)
{
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT;
#else
    return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

/* A non-blocking connect() that has not finished yet. */
static inline int sock_connect_pending(int err)
{
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
#else
    return err == EINPROGRESS;
#endif
}

/* Wait up to ms for a pending connect(): 0 connected, -1 failed or timed out. */
static inline int sock_wait_connected(int s, int ms)
{
    fd_set wf;
    fd_set ef;
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    int err = 0;
    socklen_t len = sizeof(err);

    FD_ZERO(&wf);
    FD_SET(s, &wf);
    FD_ZERO(&ef);
    FD_SET(s, &ef);     /* Windows reports a failed connect in exceptfds */
    if (select(s + 1, NULL, &wf, &ef, &tv) <= 0)
        return -1;
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0)
        return -1;
    return 0;
}

/* Wait up to ms for s to be readable: 1 readable (or closed), 0 not, -1 error. */
static inline int sock_readable(int s, int ms)
{
    fd_set rf;
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };

    FD_ZERO(&rf);
    FD_SET(s, &rf);
    return select(s + 1, &rf, NULL, NULL, &tv);
}

/* A socket error, as strerror() would give it. */
static inline const char *sock_strerror(int err)
{
#ifdef _WIN32
    static __thread char buf[200];
    size_t n;
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)err, 0, buf,
                       sizeof(buf) - 16, NULL) == 0)
        buf[0] = '\0';
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '.'))
        buf[--n] = '\0';
    snprintf(buf + n, sizeof(buf) - n, "%s(WSA %d)", n > 0 ? " " : "socket error ", err);
    return buf;
#else
    return strerror(err);
#endif
}

/* perror() for socket calls: errno is not set by Winsock, so perror would
 * print an unrelated message there. */
static inline void sock_perror(const char *msg)
{
    int err = sock_error();
    fprintf(stderr, "%s: %s\n", msg, sock_strerror(err));
}

#endif // QTR_NET_H
