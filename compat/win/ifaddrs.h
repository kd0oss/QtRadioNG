/*
 * ifaddrs.h stand-in for Windows (mingw-w64 / MSYS2).
 *
 * getifaddrs()/freeifaddrs() built on GetAdaptersAddresses(). Only IPv4
 * unicast addresses of adapters that are up are returned, which is all the
 * HPSDR discovery code asks for. ifa_name is the adapter's friendly name
 * ("Ethernet", "Wi-Fi"), truncated to fit IF_NAMESIZE. Link with iphlpapi.
 */
#ifndef QTR_WIN_IFADDRS_H
#define QTR_WIN_IFADDRS_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>

struct ifaddrs
{
    struct ifaddrs *ifa_next;
    char *ifa_name;
    unsigned int ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    struct sockaddr *ifa_broadaddr;
    void *ifa_data;
};

/* One allocation per entry: the ifaddrs header followed by its storage. */
struct qtr_ifaddrs_entry
{
    struct ifaddrs ifa;
    struct sockaddr_in addr;
    struct sockaddr_in netmask;
    struct sockaddr_in broadaddr;
    char name[IF_NAMESIZE];
};

static inline void freeifaddrs(struct ifaddrs *ifa)
{
    while (ifa != NULL)
    {
        struct ifaddrs *next = ifa->ifa_next;
        free(ifa);
        ifa = next;
    }
}

static inline int getifaddrs(struct ifaddrs **ifap)
{
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *list = NULL;
    IP_ADAPTER_ADDRESSES *ad;
    struct ifaddrs *head = NULL;
    struct ifaddrs **tail = &head;
    ULONG rc;
    int tries;

    if (ifap == NULL)
        return -1;
    *ifap = NULL;

    for (tries = 0; tries < 3; tries++)
    {
        list = (IP_ADAPTER_ADDRESSES *)malloc(size);
        if (list == NULL)
            return -1;
        rc = GetAdaptersAddresses(AF_INET,
                                  GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                  NULL, list, &size);
        if (rc != ERROR_BUFFER_OVERFLOW)
            break;
        free(list);
        list = NULL;
    }
    if (list == NULL || rc != NO_ERROR)
    {
        free(list);
        return -1;
    }

    for (ad = list; ad != NULL; ad = ad->Next)
    {
        IP_ADAPTER_UNICAST_ADDRESS *ua;
        unsigned int flags;

        if (ad->OperStatus != IfOperStatusUp)
            continue;
        flags = IFF_UP | IFF_RUNNING;
        if (ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            flags |= IFF_LOOPBACK;
        else
            flags |= IFF_BROADCAST;
        if (!(ad->Flags & IP_ADAPTER_NO_MULTICAST))
            flags |= IFF_MULTICAST;

        for (ua = ad->FirstUnicastAddress; ua != NULL; ua = ua->Next)
        {
            struct qtr_ifaddrs_entry *e;
            ULONG prefix = ua->OnLinkPrefixLength;
            ULONG mask;

            if (ua->Address.lpSockaddr == NULL || ua->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            e = (struct qtr_ifaddrs_entry *)calloc(1, sizeof(*e));
            if (e == NULL)
            {
                freeifaddrs(head);
                free(list);
                return -1;
            }
            memcpy(&e->addr, ua->Address.lpSockaddr, sizeof(e->addr));
            mask = (prefix == 0) ? 0 : (prefix >= 32 ? 0xffffffffUL : ~((1UL << (32 - prefix)) - 1));
            e->netmask.sin_family = AF_INET;
            e->netmask.sin_addr.s_addr = htonl(mask);
            e->broadaddr.sin_family = AF_INET;
            e->broadaddr.sin_addr.s_addr = e->addr.sin_addr.s_addr | ~e->netmask.sin_addr.s_addr;
            if (ad->FriendlyName == NULL
                || WideCharToMultiByte(CP_UTF8, 0, ad->FriendlyName, -1, e->name, sizeof(e->name), NULL, NULL) == 0)
            {
                strncpy(e->name, ad->AdapterName != NULL ? ad->AdapterName : "?", sizeof(e->name) - 1);
            }
            e->name[sizeof(e->name) - 1] = '\0';

            e->ifa.ifa_name = e->name;
            e->ifa.ifa_flags = flags;
            e->ifa.ifa_addr = (struct sockaddr *)&e->addr;
            e->ifa.ifa_netmask = (struct sockaddr *)&e->netmask;
            e->ifa.ifa_broadaddr = (flags & IFF_BROADCAST) ? (struct sockaddr *)&e->broadaddr : NULL;
            *tail = &e->ifa;
            tail = &e->ifa.ifa_next;
        }
    }
    free(list);
    *ifap = head;
    return 0;
}

#endif
