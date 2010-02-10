/* Portions of this file are subject to the following copyright(s).  See
 * the Net-SNMP's COPYING file for more details and other copyrights
 * that may apply:
 */
/*
 * Portions of this file are copyrighted by:
 * Copyright Copyright 2003 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms specified in the COPYING file
 * distributed with the Net-SNMP package.
 */

#include <net-snmp/net-snmp-config.h>

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>

#if HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if HAVE_NETDB_H
#include <netdb.h>
#endif
#if HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#if HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#include <net-snmp/types.h>
#include <net-snmp/output_api.h>
#include <net-snmp/config_api.h>

#include <net-snmp/library/snmp_transport.h>
#include <net-snmp/library/snmpUDPDomain.h>
#include <net-snmp/library/system.h>
#include <net-snmp/library/tools.h>

#include "inet_ntop.h"
#include "inet_pton.h"

#ifndef INADDR_NONE
#define INADDR_NONE	-1
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

static netsnmp_tdomain udpDomain;

/*
 * needs to be in sync with the definitions in snmplib/snmpTCPDomain.c 
 * and perl/agent/agent.xs 
 */
typedef netsnmp_indexed_addr_pair netsnmp_udp_addr_pair;

/*
 * not static, since snmpUDPIPv6Domain needs it, but not public, either.
 * (ie don't put it in a public header.)
 */
void _netsnmp_udp_sockopt_set(int fd, int server);
int
netsnmp_sockaddr_in2(struct sockaddr_in *addr,
                     const char *inpeername, const char *default_target);

/*
 * Return a string representing the address in data, or else the "far end"
 * address if data is NULL.  
 */

char *
netsnmp_udp_fmtaddr(netsnmp_transport *t, void *data, int len)
{
    return netsnmp_ipv4_fmtaddr("UDP", t, data, len);
}



#if defined(linux) && defined(IP_PKTINFO)

# define netsnmp_dstaddr(x) (&(((struct in_pktinfo *)(CMSG_DATA(x)))->ipi_addr))

int netsnmp_udp_recvfrom(int s, void *buf, int len, struct sockaddr *from, socklen_t *fromlen, struct in_addr *dstip, int *if_index)
{
    int r;
    struct iovec iov[1];
    char cmsg[CMSG_SPACE(sizeof(struct in_pktinfo))];
    struct cmsghdr *cmsgptr;
    struct msghdr msg;

    iov[0].iov_base = buf;
    iov[0].iov_len = len;

    memset(&msg, 0, sizeof msg);
    msg.msg_name = from;
    msg.msg_namelen = *fromlen;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = &cmsg;
    msg.msg_controllen = sizeof(cmsg);

    r = recvmsg(s, &msg, NETSNMP_DONTWAIT);

    if (r == -1) {
        return -1;
    }

    DEBUGMSGTL(("netsnmp_udp", "got source addr: %s\n", inet_ntoa(((struct sockaddr_in *)from)->sin_addr)));
    for (cmsgptr = CMSG_FIRSTHDR(&msg); cmsgptr != NULL; cmsgptr = CMSG_NXTHDR(&msg, cmsgptr)) {
        if (cmsgptr->cmsg_level == SOL_IP && cmsgptr->cmsg_type == IP_PKTINFO) {
            memcpy((void *) dstip, netsnmp_dstaddr(cmsgptr), sizeof(struct in_addr));
            *if_index = (((struct in_pktinfo *)(CMSG_DATA(cmsgptr)))->ipi_ifindex);
            DEBUGMSGTL(("netsnmp_udp", "got destination (local) addr %s, iface %d\n",
                    inet_ntoa(*dstip), *if_index));
        }
    }
    return r;
}

int netsnmp_udp_sendto(int fd, struct in_addr *srcip, int if_index, struct sockaddr *remote,
                void *data, int len)
{
    struct iovec iov = { data, len };
    struct {
        struct cmsghdr cm;
        struct in_pktinfo ipi;
    } cmsg;
    struct msghdr m;
    int ret;

    memset(&cmsg, 0, sizeof(cmsg));
    cmsg.cm.cmsg_len = sizeof(struct cmsghdr) + sizeof(struct in_pktinfo);
    cmsg.cm.cmsg_level = SOL_IP;
    cmsg.cm.cmsg_type = IP_PKTINFO;
    cmsg.ipi.ipi_ifindex = if_index;
    cmsg.ipi.ipi_spec_dst.s_addr = (srcip ? srcip->s_addr : INADDR_ANY);

    m.msg_name		= remote;
    m.msg_namelen	= sizeof(struct sockaddr_in);
    m.msg_iov		= &iov;
    m.msg_iovlen	= 1;
    m.msg_control	= &cmsg;
    m.msg_controllen	= sizeof(cmsg);
    m.msg_flags		= 0;

    DEBUGMSGTL(("netsnmp_udp", "netsnmp_udp_sendto: sending from %s iface %d\n",
            (srcip ? inet_ntoa(*srcip) : "NULL"), if_index));
    errno = 0;
    ret = sendmsg(fd, &m, MSG_NOSIGNAL|MSG_DONTWAIT);
    if (ret < 0 && errno == EINVAL && srcip) {
        /* The error might be caused by broadcast srcip (i.e. we're responding
         * to broadcast request) - sendmsg does not like it. Try to resend it
         * with global address. */
        cmsg.ipi.ipi_spec_dst.s_addr = INADDR_ANY;
        DEBUGMSGTL(("netsnmp_udp",
                "netsnmp_udp_sendto: re-sending the message\n"));
        ret = sendmsg(fd, &m, MSG_NOSIGNAL|MSG_DONTWAIT);
    }
    return ret;
}
#endif /* linux && IP_PKTINFO */

/*
 * You can write something into opaque that will subsequently get passed back 
 * to your send function if you like.  For instance, you might want to
 * remember where a PDU came from, so that you can send a reply there...  
 */

static int
netsnmp_udp_recv(netsnmp_transport *t, void *buf, int size,
		 void **opaque, int *olength)
{
    int             rc = -1;
    socklen_t       fromlen = sizeof(struct sockaddr);
    netsnmp_udp_addr_pair *addr_pair = NULL;
    struct sockaddr *from;

    if (t != NULL && t->sock >= 0) {
        addr_pair = (netsnmp_udp_addr_pair *) malloc(sizeof(netsnmp_udp_addr_pair));
        if (addr_pair == NULL) {
            *opaque = NULL;
            *olength = 0;
            return -1;
        } else {
            memset(addr_pair, 0, sizeof(netsnmp_udp_addr_pair));
            from = (struct sockaddr *) &(addr_pair->remote_addr);
        }

	while (rc < 0) {
#if defined(linux) && defined(IP_PKTINFO)
            rc = netsnmp_udp_recvfrom(t->sock, buf, size, from, &fromlen,
                    &(addr_pair->local_addr), &(addr_pair->if_index));
#else
            rc = recvfrom(t->sock, buf, size, NETSNMP_DONTWAIT, from, &fromlen);
#endif /* linux && IP_PKTINFO */
	    if (rc < 0 && errno != EINTR) {
		break;
	    }
	}

        if (rc >= 0) {
            char *str = netsnmp_udp_fmtaddr(NULL, addr_pair, sizeof(netsnmp_udp_addr_pair));
            DEBUGMSGTL(("netsnmp_udp",
			"recvfrom fd %d got %d bytes (from %s)\n",
			t->sock, rc, str));
            free(str);
        } else {
            DEBUGMSGTL(("netsnmp_udp", "recvfrom fd %d err %d (\"%s\")\n",
                        t->sock, errno, strerror(errno)));
        }
        *opaque = (void *)addr_pair;
        *olength = sizeof(netsnmp_udp_addr_pair);
    }
    return rc;
}



static int
netsnmp_udp_send(netsnmp_transport *t, void *buf, int size,
		 void **opaque, int *olength)
{
    int rc = -1;
    netsnmp_udp_addr_pair *addr_pair = NULL;
    struct sockaddr *to = NULL;

    if (opaque != NULL && *opaque != NULL &&
        *olength == sizeof(netsnmp_udp_addr_pair)) {
        addr_pair = (netsnmp_udp_addr_pair *) (*opaque);
    } else if (t != NULL && t->data != NULL &&
                t->data_length == sizeof(netsnmp_udp_addr_pair)) {
        addr_pair = (netsnmp_udp_addr_pair *) (t->data);
    }

    to = (struct sockaddr *) &(addr_pair->remote_addr);

    if (to != NULL && t != NULL && t->sock >= 0) {
        char *str = netsnmp_udp_fmtaddr(NULL, (void *) addr_pair,
                                        sizeof(netsnmp_udp_addr_pair));
        DEBUGMSGTL(("netsnmp_udp", "send %d bytes from %p to %s on fd %d\n",
                    size, buf, str, t->sock));
        free(str);
	while (rc < 0) {
#if defined(linux) && defined(IP_PKTINFO)
            rc = netsnmp_udp_sendto(t->sock,
                    addr_pair ? &(addr_pair->local_addr) : NULL,
                    addr_pair ? addr_pair->if_index : 0, to, buf, size);
#else
            rc = sendto(t->sock, buf, size, 0, to, sizeof(struct sockaddr));
#endif /* linux && IP_PKTINFO */
	    if (rc < 0 && errno != EINTR) {
                DEBUGMSGTL(("netsnmp_udp", "sendto error, rc %d (errno %d)\n",
                            rc, errno));
		break;
	    }
	}
    }
    return rc;
}


/*
 * find largest possible buffer between current size and specified size.
 *
 * Try to maximize the current buffer of type "optname"
 * to the maximum allowable size by the OS (as close to
 * size as possible)
 */
static int
_sock_buffer_maximize(int s, int optname, const char *buftype, int size)
{
    int            curbuf = 0;
    socklen_t      curbuflen = sizeof(int);
    int            lo, mid, hi;

    /*
     * First we need to determine our current buffer
     */
    if ((getsockopt(s, SOL_SOCKET, optname, (void *) &curbuf,
                    &curbuflen) == 0) 
            && (curbuflen == sizeof(int))) {

        DEBUGMSGTL(("verbose:socket:buffer:max", "Current %s is %d\n",
                    buftype, curbuf));

        /*
         * Let's not be stupid ... if we were asked for less than what we
         * already have, then forget about it
         */
        if (size <= curbuf) {
            DEBUGMSGTL(("verbose:socket:buffer:max",
                        "Requested %s <= current buffer\n", buftype));
            return curbuf;
        }

        /*
         * Do a binary search the optimal buffer within 1k of the point of
         * failure. This is rather bruteforce, but simple
         */
        hi = size;
        lo = curbuf;

        while (hi - lo > 1024) {
            mid = (lo + hi) / 2;
            if (setsockopt(s, SOL_SOCKET, optname, (void *) &mid,
                        sizeof(int)) == 0) {
                lo = mid; /* Success: search between mid and hi */
            } else {
                hi = mid; /* Failed: search between lo and mid */
            }
        }

        /*
         * Now print if this optimization helped or not
         */
        if (getsockopt(s,SOL_SOCKET, optname, (void *) &curbuf,
                    &curbuflen) == 0) {
            DEBUGMSGTL(("socket:buffer:max", 
                        "Maximized %s: %d\n",buftype, curbuf));
        } 
    } else {
        /*
         * There is really not a lot we can do anymore.
         * If the OS doesn't give us the current buffer, then what's the 
         * point in trying to make it better
         */
        DEBUGMSGTL(("socket:buffer:max", "Get %s failed ... giving up!\n",
                    buftype));
        curbuf = -1;
    }

    return curbuf;
}


static const char *
_sock_buf_type_get(int optname, int local)
{
    if (optname == SO_SNDBUF) {
        if (local)
            return "server send buffer";
        else
            return "client send buffer";
    } else if (optname == SO_RCVBUF) {
        if (local)
            return "server receive buffer";
        else
            return "client receive buffer";
    }

    return "unknown buffer";
}

/*
 *
 * Get the requested buffersize, based on
 * - sockettype : client (local = 0) or server (local = 1) 
 * - buffertype : send (optname = SO_SNDBUF) or recv (SO_RCVBUF)
 *
 * In case a compile time buffer was specified, then use that one
 * if there was no runtime configuration override
 */
static int
_sock_buffer_size_get(int optname, int local, const char **buftype)
{
    int size;

    if (NULL != buftype)
        *buftype = _sock_buf_type_get(optname, local);

    if (optname == SO_SNDBUF) {
        if (local) {
            size = netsnmp_ds_get_int(NETSNMP_DS_LIBRARY_ID, 
                    NETSNMP_DS_LIB_SERVERSENDBUF);
#ifdef NETSNMP_DEFAULT_SERVER_SEND_BUF
            if (size <= 0)
               size = NETSNMP_DEFAULT_SERVER_SEND_BUF;
#endif
        } else {
            size = netsnmp_ds_get_int(NETSNMP_DS_LIBRARY_ID, 
                    NETSNMP_DS_LIB_CLIENTSENDBUF);
#ifdef NETSNMP_DEFAULT_CLIENT_SEND_BUF
            if (size <= 0)
               size = NETSNMP_DEFAULT_CLIENT_SEND_BUF;
#endif
        }
    } else if (optname == SO_RCVBUF) {
        if (local) {
            size = netsnmp_ds_get_int(NETSNMP_DS_LIBRARY_ID, 
                    NETSNMP_DS_LIB_SERVERRECVBUF);
#ifdef NETSNMP_DEFAULT_SERVER_RECV_BUF
            if (size <= 0)
               size = NETSNMP_DEFAULT_SERVER_RECV_BUF;
#endif
        } else {
            size = netsnmp_ds_get_int(NETSNMP_DS_LIBRARY_ID, 
                    NETSNMP_DS_LIB_CLIENTRECVBUF);
#ifdef NETSNMP_DEFAULT_CLIENT_RECV_BUF
            if (size <= 0)
               size = NETSNMP_DEFAULT_CLIENT_RECV_BUF;
#endif
        }
    } else {
        size = 0;
    }

    DEBUGMSGTL(("socket:buffer", "Requested %s is %d\n",
                (buftype) ? *buftype : "unknown buffer", size));

    return(size);
}

/*
 * set socket buffer size
 *
 * @param ss     : socket
 * @param optname: SO_SNDBUF or SO_RCVBUF
 * @param local  : 1 for server, 0 for client
 * @param reqbuf : requested size, or 0 for default
 *
 * @retval    -1 : error
 * @retval    >0 : new buffer size
 */
int
netsnmp_sock_buffer_set(int s, int optname, int local, int size)
{
#if ! defined(SO_SNDBUF) && ! defined(SO_RCVBUF)
    DEBUGMSGTL(("socket:buffer", "Changing socket buffer is not supported\n"));
    return -1;
#else
    const char     *buftype;
    int            curbuf = 0;
    socklen_t      curbuflen = sizeof(int);

#   ifndef  SO_SNDBUF
    if (SO_SNDBUF == optname) {
        DEBUGMSGTL(("socket:buffer",
                    "Changing socket send buffer is not supported\n"));
        return -1;
    }
#   endif                          /*SO_SNDBUF */
#   ifndef  SO_RCVBUF
    if (SO_RCVBUF == optname) {
        DEBUGMSGTL(("socket:buffer",
                    "Changing socket receive buffer is not supported\n"));
        return -1;
    }
#   endif                          /*SO_RCVBUF */

    /*
     * What is the requested buffer size ?
     */
    if (0 == size)
        size = _sock_buffer_size_get(optname, local, &buftype);
    else {
        buftype = _sock_buf_type_get(optname, local);
        DEBUGMSGT(("verbose:socket:buffer", "Requested %s is %d\n",
                   buftype, size));
    }

    if ((getsockopt(s, SOL_SOCKET, optname, (void *) &curbuf,
                    &curbuflen) == 0) 
        && (curbuflen == sizeof(int))) {
        
        DEBUGMSGT(("verbose:socket:buffer", "Original %s is %d\n",
                   buftype, curbuf));
        if (curbuf >= size) {
            DEBUGMSGT(("verbose:socket:buffer",
                      "New %s size is smaller than original!\n", buftype));
        }
    }

    /*
     * If the buffersize was not specified or it was a negative value
     * then don't change the OS buffers at all
     */
    if (size <= 0) {
       DEBUGMSGT(("socket:buffer",
                    "%s not valid or not specified; using OS default(%d)\n",
                    buftype,curbuf));
       return curbuf;
    }

    /*
     * Try to set the requested send buffer
     */
    if (setsockopt(s, SOL_SOCKET, optname, (void *) &size, sizeof(int)) == 0) {
        /*
         * Because some platforms lie about the actual buffer that has been 
         * set (Linux will always say it worked ...), we print some 
         * diagnostic output for debugging
         */
        DEBUGIF("socket:buffer") {
            DEBUGMSGT(("socket:buffer", "Set %s to %d\n",
                       buftype, size));
            if ((getsockopt(s, SOL_SOCKET, optname, (void *) &curbuf,
                            &curbuflen) == 0) 
                    && (curbuflen == sizeof(int))) {

                DEBUGMSGT(("verbose:socket:buffer",
                           "Now %s is %d\n", buftype, curbuf));
            }
        }
        /*
         * If the new buffer is smaller than the size we requested, we will
         * try to increment the new buffer with 1k increments 
         * (this will sometime allow us to reach a more optimal buffer.)
         *   For example : On Solaris, if the max OS buffer is 100k and you
         *   request 110k, you end up with the default 8k :-(
         */
        if (curbuf < size) {
            curbuf = _sock_buffer_maximize(s, optname, buftype, size);
            if(-1 != curbuf)
                size = curbuf;
        }

    } else {
        /*
         * Obviously changing the buffer failed, most like like because we 
         * requested a buffer greater than the OS limit.
         * Therefore we need to search for an optimal buffer that is close
         * enough to the point of failure.
         * This will allow us to reach a more optimal buffer.
         *   For example : On Solaris, if the max OS buffer is 100k and you 
         *   request 110k, you end up with the default 8k :-(
         *   After this quick seach we would get 1k close to 100k (the max)
         */
        DEBUGMSGTL(("socket:buffer", "couldn't set %s to %d\n",
                    buftype, size));

        curbuf = _sock_buffer_maximize(s, optname, buftype, size);
        if(-1 != curbuf)
            size = curbuf;
    }

    return size;
#endif
}

/*
 * Open a UDP-based transport for SNMP.  Local is TRUE if addr is the local
 * address to bind to (i.e. this is a server-type session); otherwise addr is 
 * the remote address to send things to.  
 */

netsnmp_transport *
netsnmp_udp_transport(struct sockaddr_in *addr, int local)
{
    netsnmp_transport *t = NULL;

    t = netsnmp_udpipv4base_transport(addr, local);
    if (NULL == t) {
        return NULL;
    }

    /*
     * Set Domain
     */

    t->domain = netsnmpUDPDomain;
    t->domain_length = netsnmpUDPDomain_len;

    /*
     * 16-bit length field, 8 byte UDP header, 20 byte IPv4 header  
     */

    t->msgMaxSize = 0xffff - 8 - 20;
    t->f_recv     = netsnmp_udp_recv;
    t->f_send     = netsnmp_udp_send;
    t->f_close    = netsnmp_socketbase_close;
    t->f_accept   = NULL;
    t->f_fmtaddr  = netsnmp_udp_fmtaddr;

    return t;
}

#if !defined(NETSNMP_DISABLE_SNMPV1) || !defined(NETSNMP_DISABLE_SNMPV2C)
/*
 * The following functions provide the "com2sec" configuration token
 * functionality for compatibility.
 */

#define EXAMPLE_NETWORK		"NETWORK"
#define EXAMPLE_COMMUNITY	"COMMUNITY"

typedef struct com2SecEntry_s {
    const char *secName;
    const char *contextName;
    struct com2SecEntry_s *next;
    in_addr_t   network;
    in_addr_t   mask;
    const char  community[1];
} com2SecEntry;

static com2SecEntry   *com2SecList = NULL, *com2SecListLast = NULL;

void
netsnmp_udp_parse_security(const char *token, char *param)
{
    char            secName[VACMSTRINGLEN + 1];
    size_t          secNameLen;
    char            contextName[VACMSTRINGLEN + 1];
    size_t          contextNameLen;
    char            community[COMMUNITY_MAX_LEN + 1];
    size_t          communityLen;
    char            source[270]; /* dns-name(253)+/(1)+mask(15)+\0(1) */
    struct in_addr  network, mask;

    /*
     * Get security, source address/netmask and community strings.
     */

    param = copy_nword( param, secName, sizeof(secName));
    if (strcmp(secName, "-Cn") == 0) {
        if (!param) {
            config_perror("missing CONTEXT_NAME parameter");
            return;
        }
        param = copy_nword( param, contextName, sizeof(contextName));
        contextNameLen = strlen(contextName) + 1;
        if (contextNameLen > VACMSTRINGLEN) {
            config_perror("context name too long");
            return;
        }
        if (!param) {
            config_perror("missing NAME parameter");
            return;
        }
        param = copy_nword( param, secName, sizeof(secName));
    } else {
        contextNameLen = 0;
    }

    secNameLen = strlen(secName) + 1;
    if (secNameLen == 1) {
        config_perror("empty NAME parameter");
        return;
    } else if (secNameLen > VACMSTRINGLEN) {
        config_perror("security name too long");
        return;
    }

    if (!param) {
        config_perror("missing SOURCE parameter");
        return;
    }
    param = copy_nword( param, source, sizeof(source));
    if (source[0] == '\0') {
        config_perror("empty SOURCE parameter");
        return;
    }
    if (strncmp(source, EXAMPLE_NETWORK, strlen(EXAMPLE_NETWORK)) == 0) {
        config_perror("example config NETWORK not properly configured");
        return;
    }

    if (!param) {
        config_perror("missing COMMUNITY parameter");
        return;
    }
    param = copy_nword( param, community, sizeof(community));
    if (community[0] == '\0') {
        config_perror("empty COMMUNITY parameter");
        return;
    }
    communityLen = strlen(community) + 1;
    if (communityLen >= COMMUNITY_MAX_LEN) {
        config_perror("community name too long");
        return;
    }
    if (communityLen == sizeof(EXAMPLE_COMMUNITY) &&
        memcmp(community, EXAMPLE_COMMUNITY, sizeof(EXAMPLE_COMMUNITY)) == 0) {
        config_perror("example config COMMUNITY not properly configured");
        return;
    }

    /* Deal with the "default" case first. */
    if (strcmp(source, "default") == 0) {
        network.s_addr = 0;
        mask.s_addr = 0;
    } else {
        /* Split the source/netmask parts */
        char *strmask = strchr(source, '/');
        if (strmask != NULL)
            /* Mask given. */
            *strmask++ = '\0';

        /* Try interpreting as a dotted quad. */
        if (inet_pton(AF_INET, source, &network) == 0) {
            /* Nope, wasn't a dotted quad.  Must be a hostname. */
            int ret = netsnmp_gethostbyname_v4(source, &network.s_addr);
            if (ret < 0) {
                config_perror("cannot resolve source hostname");
                return;
            }
        }

        /* Now work out the mask. */
        if (strmask == NULL || *strmask == '\0') {
            /* No mask was given. Assume /32 */
            mask.s_addr = (in_addr_t)(~0UL);
        } else {
            /* Try to interpret mask as a "number of 1 bits". */
            char* cp;
            long maskLen = strtol(strmask, &cp, 10);
            if (*cp == '\0') {
                if (0 < maskLen && maskLen <= 32)
                    mask.s_addr = htonl((in_addr_t)(~0UL << (32 - maskLen)));
                else {
                    config_perror("bad mask length");
                    return;
                }
            }
            /* Try to interpret mask as a dotted quad. */
            else if (inet_pton(AF_INET, strmask, &mask) == 0) {
                config_perror("bad mask");
                return;
            }

            /* Check that the network and mask are consistent. */
            if (network.s_addr & ~mask.s_addr) {
                config_perror("source/mask mismatch");
                return;
            }
        }
    }

    {
        void* v = malloc(offsetof(com2SecEntry, community) + communityLen +
                         secNameLen + contextNameLen);

        com2SecEntry* e = (com2SecEntry*)v;
        char* last = ((char*)v) + offsetof(com2SecEntry, community);

        if (v == NULL) {
            config_perror("memory error");
            return;
        }

        /*
         * Everything is okay.  Copy the parameters to the structure allocated
         * above and add it to END of the list.
         */

        {
          char buf1[INET_ADDRSTRLEN];
          char buf2[INET_ADDRSTRLEN];
          DEBUGMSGTL(("netsnmp_udp_parse_security",
                      "<\"%s\", %s/%s> => \"%s\"\n", community,
                      inet_ntop(AF_INET, &network, buf1, sizeof(buf1)),
                      inet_ntop(AF_INET, &mask, buf2, sizeof(buf2)),
                      secName));
        }

        memcpy(last, community, communityLen);
        last += communityLen;
        memcpy(last, secName, secNameLen);
        e->secName = last;
        last += secNameLen;
        if (contextNameLen) {
            memcpy(last, contextName, contextNameLen);
            e->contextName = last;
        } else
            e->contextName = last - 1;
        e->network = network.s_addr;
        e->mask = mask.s_addr;
        e->next = NULL;

        if (com2SecListLast != NULL) {
            com2SecListLast->next = e;
            com2SecListLast = e;
        } else {
            com2SecListLast = com2SecList = e;
        }
    }
}

void
netsnmp_udp_com2SecList_free(void)
{
    com2SecEntry   *e = com2SecList;
    while (e != NULL) {
        com2SecEntry   *tmp = e;
        e = e->next;
        free(tmp);
    }
    com2SecList = com2SecListLast = NULL;
}
#endif /* support for community based SNMP */

void
netsnmp_udp_agent_config_tokens_register(void)
{
#if !defined(NETSNMP_DISABLE_SNMPV1) || !defined(NETSNMP_DISABLE_SNMPV2C)
    register_app_config_handler("com2sec", netsnmp_udp_parse_security,
                                netsnmp_udp_com2SecList_free,
                                "[-Cn CONTEXT] secName IPv4-network-address[/netmask] community");
#endif /* support for community based SNMP */
}



/*
 * Return 0 if there are no com2sec entries, or return 1 if there ARE com2sec
 * entries.  On return, if a com2sec entry matched the passed parameters,
 * then *secName points at the appropriate security name, or is NULL if the
 * parameters did not match any com2sec entry.
 */

#if !defined(NETSNMP_DISABLE_SNMPV1) || !defined(NETSNMP_DISABLE_SNMPV2C)
int
netsnmp_udp_getSecName(void *opaque, int olength,
                       const char *community,
                       size_t community_len, const char **secName,
                       const char **contextName)
{
    const com2SecEntry *c;
    netsnmp_udp_addr_pair *addr_pair = (netsnmp_udp_addr_pair *) opaque;
    struct sockaddr_in *from = (struct sockaddr_in *) &(addr_pair->remote_addr);
    char           *ztcommunity = NULL;

    if (secName != NULL) {
        *secName = NULL;  /* Haven't found anything yet */
    }

    /*
     * Special case if there are NO entries (as opposed to no MATCHING
     * entries).
     */

    if (com2SecList == NULL) {
        DEBUGMSGTL(("netsnmp_udp_getSecName", "no com2sec entries\n"));
        return 0;
    }

    /*
     * If there is no IPv4 source address, then there can be no valid security
     * name.
     */

   DEBUGMSGTL(("netsnmp_udp_getSecName", "opaque = %p (len = %d), sizeof = %d, family = %d (%d)\n",
   opaque, olength, (int)sizeof(netsnmp_udp_addr_pair), from->sin_family, AF_INET));
    if (opaque == NULL || olength != sizeof(netsnmp_udp_addr_pair) ||
        from->sin_family != AF_INET) {
        DEBUGMSGTL(("netsnmp_udp_getSecName",
		    "no IPv4 source address in PDU?\n"));
        return 1;
    }

    DEBUGIF("netsnmp_udp_getSecName") {
	ztcommunity = (char *)malloc(community_len + 1);
	if (ztcommunity != NULL) {
	    memcpy(ztcommunity, community, community_len);
	    ztcommunity[community_len] = '\0';
	}

	DEBUGMSGTL(("netsnmp_udp_getSecName", "resolve <\"%s\", 0x%08x>\n",
		    ztcommunity ? ztcommunity : "<malloc error>",
		    from->sin_addr.s_addr));
    }

    for (c = com2SecList; c != NULL; c = c->next) {
        {
            char buf1[INET_ADDRSTRLEN];
            char buf2[INET_ADDRSTRLEN];
            DEBUGMSGTL(("netsnmp_udp_getSecName","compare <\"%s\", %s/%s>",
                        c->community,
                        inet_ntop(AF_INET, &c->network, buf1, sizeof(buf1)),
                        inet_ntop(AF_INET, &c->mask, buf2, sizeof(buf2))));
        }
        if ((community_len == strlen(c->community)) &&
	    (memcmp(community, c->community, community_len) == 0) &&
            ((from->sin_addr.s_addr & c->mask) == c->network)) {
            DEBUGMSG(("netsnmp_udp_getSecName", "... SUCCESS\n"));
            if (secName != NULL) {
                *secName = c->secName;
                *contextName = c->contextName;
            }
            break;
        }
        DEBUGMSG(("netsnmp_udp_getSecName", "... nope\n"));
    }
    if (ztcommunity != NULL) {
        free(ztcommunity);
    }
    return 1;
}
#endif /* support for community based SNMP */


netsnmp_transport *
netsnmp_udp_create_tstring(const char *str, int local,
			   const char *default_target)
{
    struct sockaddr_in addr;

    if (netsnmp_sockaddr_in2(&addr, str, default_target)) {
        return netsnmp_udp_transport(&addr, local);
    } else {
        return NULL;
    }
}


netsnmp_transport *
netsnmp_udp_create_ostring(const u_char * o, size_t o_len, int local)
{
    struct sockaddr_in addr;

    if (o_len == 6) {
        unsigned short porttmp = (o[4] << 8) + o[5];
        addr.sin_family = AF_INET;
        memcpy((u_char *) & (addr.sin_addr.s_addr), o, 4);
        addr.sin_port = htons(porttmp);
        return netsnmp_udp_transport(&addr, local);
    }
    return NULL;
}


void
netsnmp_udp_ctor(void)
{
    udpDomain.name = netsnmpUDPDomain;
    udpDomain.name_length = netsnmpUDPDomain_len;
    udpDomain.prefix = (const char**)calloc(2, sizeof(char *));
    udpDomain.prefix[0] = "udp";

    udpDomain.f_create_from_tstring_new = netsnmp_udp_create_tstring;
    udpDomain.f_create_from_ostring = netsnmp_udp_create_ostring;

    netsnmp_tdomain_register(&udpDomain);
}
