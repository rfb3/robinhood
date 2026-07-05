//
// netifs.c - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

#include "robinhood.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>

// `address` is already rendered to text (inet_ntop()) at insertion
// time, since the `struct ifaddrs` list it came from is freed before
// this example is done using the table.
struct interface_info
{
    unsigned int flags;
    char         address [INET6_ADDRSTRLEN];
};

// ===========================================================================
// File-local prototypes
// ===========================================================================

static void
build_key(char*       key_buffer,
          size_t      key_buffer_size,
          RHTable     table,
          const char* name,
          const char* family);

static int
compare_keys(const void* left, const void* right);

static void
format_flags(char* buffer, size_t buffer_size, unsigned int flags);

int
main(int argc, char** argv);

static void
print_usage(const char* program_name);

// Real interfaces often carry more than one address of the same
// family (e.g. an IPv6 link-local address alongside a global one),
// so name+family alone isn't always unique.
static void
build_key(char*       key_buffer,
          size_t      key_buffer_size,
          RHTable     table,
          const char* name,
          const char* family)
{
    snprintf(key_buffer, key_buffer_size, "%s/%s", name, family);

    for (int suffix = 1; rh_has(table, key_buffer); ++suffix)
    {
        snprintf(key_buffer, key_buffer_size, "%s/%s#%d", name, family,
                 suffix);
    }
}

// `left`/`right` each point at one array element (i.e. a `const
// char*`), not at the string itself -- hence the double indirection.
static int
compare_keys(const void* left, const void* right)
{
    const char* const* left_key  = (const char* const*)left;
    const char* const* right_key = (const char* const*)right;

    return strcmp(*left_key, *right_key);
}

// Real interfaces carry many more IFF_* bits than this, but these
// three are the ones most useful for "is this connection actually
// usable" at a glance.
static void
format_flags(char* buffer, size_t buffer_size, unsigned int flags)
{
    static const struct
    {
        unsigned int bit;
        const char*  name;
    } known_flags [] = {
        {IFF_UP, "UP"},
        {IFF_LOOPBACK, "LOOPBACK"},
        {IFF_RUNNING, "RUNNING"},
    };

    size_t position   = 0;
    size_t flag_count = sizeof(known_flags) / sizeof(known_flags [0]);
    buffer [0]        = '\0';

    for (size_t index = 0; index < flag_count; ++index)
    {
        if ((flags & known_flags [index].bit) == 0)
        {
            continue;
        }

        int written =
            snprintf(buffer + position, buffer_size - position, "%s%s",
                     (position == 0) ? "" : ",", known_flags [index].name);

        if ((written < 0) || (((size_t)written) >= (buffer_size - position)))
        {
            break;
        }

        position += (size_t)written;
    }
}

int
main(int argc, char** argv)
{
    if (argc != 1)
    {
        print_usage(argv [0]);
        return 2;
    }

    struct ifaddrs* addresses;

    if (getifaddrs(&addresses) != 0)
    {
        fprintf(stderr, "%s: getifaddrs: %s\n", argv [0], strerror(errno));
        return 1;
    }

    RHTable table = rh_create(32);

    for (struct ifaddrs* entry = addresses; entry != NULL;
         entry                 = entry->ifa_next)
    {
        if (entry->ifa_addr == NULL)
        {
            continue;
        }

        const char* family_name;
        char        address [INET6_ADDRSTRLEN];

        if (entry->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in* in_address =
                (struct sockaddr_in*)(entry->ifa_addr);

            family_name = "inet";
            inet_ntop(AF_INET, &(in_address->sin_addr), address,
                      sizeof(address));
        }
        else if (entry->ifa_addr->sa_family == AF_INET6)
        {
            struct sockaddr_in6* in6_address =
                (struct sockaddr_in6*)(entry->ifa_addr);

            family_name = "inet6";
            inet_ntop(AF_INET6, &(in6_address->sin6_addr), address,
                      sizeof(address));
        }
        else
        {
            // Link-layer or other -- see print_usage() for why these
            // are skipped.
            continue;
        }

        char key [128];
        build_key(key, sizeof(key), table, entry->ifa_name, family_name);

        struct interface_info* info =
            (struct interface_info*)(malloc(sizeof(struct interface_info)));

        if (info == NULL)
        {
            continue;
        }

        info->flags = entry->ifa_flags;
        memcpy(info->address, address, sizeof(address));

        if (!rh_set(table, key, info))
        {
            free(info);
        }
    }

    freeifaddrs(addresses);

    size_t entry_count = rh_count(table);

    printf("%zu address%s across this machine's interfaces:\n\n", entry_count,
           (entry_count == 1) ? "" : "es");

    const char** keys =
        (const char**)(malloc(entry_count * sizeof(const char*)));

    if ((keys == NULL) && (entry_count > 0))
    {
        rh_destroy(&table);
        return 1;
    }

    size_t     index = 0;
    RHIterator it;

    for (it = rhi_create(table); !rhi_is_finished(it); rhi_advance(it))
    {
        keys [index] = rhi_key(it);
        ++index;
    }

    rhi_destroy(it);

    qsort(keys, entry_count, sizeof(const char*), compare_keys);

    for (size_t rank = 0; rank < entry_count; ++rank)
    {
        struct interface_info* info =
            (struct interface_info*)(rh_get(table, keys [rank], NULL));
        char flags_text [64];

        format_flags(flags_text, sizeof(flags_text), info->flags);
        printf("%-20s %-40s %s\n", keys [rank], info->address, flags_text);

        free(info);
    }

    free(keys);
    rh_destroy(&table);

    return 0;
}

static void
print_usage(const char* program_name)
{
    fprintf(stderr,
            "usage: %s\n"
            "\n"
            "Enumerates this machine's network interfaces via"
            " getifaddrs(3),\n"
            "mapping each (interface, address family) pair to its"
            " address and\n"
            "flags in an RHTable, then prints them sorted by key."
            " IPv4/IPv6\n"
            "addresses only -- link-layer entries (MAC addresses)"
            " are skipped,\n"
            "since their sockaddr representation isn't portable"
            " (sockaddr_dl\n"
            "on BSD/macOS vs. sockaddr_ll on Linux).\n",
            program_name);
}
