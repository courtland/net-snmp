/*
 *  ICMP MIB group implementation - icmp.c
 *
 */

#include "mib_module_config.h"

#include <config.h>
#include <sys/types.h>
#include <sys/socket.h>

#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if defined(IFNET_NEEDS_KERNEL) && !defined(_KERNEL)
#define _KERNEL 1
#define _I_DEFINED_KERNEL
#endif
#include <net/if.h>
#if HAVE_NET_IF_VAR_H
#include <net/if_var.h>
#endif
#ifdef _I_DEFINED_KERNEL
#undef _KERNEL
#endif
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#include <netinet/ip_icmp.h>
#if HAVE_NETINET_ICMP_VAR_H
#include <netinet/icmp_var.h>
#endif

#if HAVE_INET_MIB2_H
#include <inet/mib2.h>
#endif
#ifdef solaris2
#include "kernel_sunos5.h"
#endif
#include "../../snmplib/system.h"

#include "mibincl.h"
#include "snmp_api.h"
#include <nlist.h>

#ifdef hpux
#undef OBJID
#include <sys/mib.h>
#include <netinet/mib_kern.h>
#undef  OBJID
#define OBJID                   ASN_OBJECT_ID
#endif /* hpux */

/* #include "../common_header.h" */

#include "icmp.h"


	/*********************
	 *
	 *  Kernel & interface information,
	 *   and internal forward declarations
	 *
	 *********************/

#if !defined(linux) && !defined(HAVE_SYS_TCPIPSTATS_H)
static struct nlist icmp_nl[] = {
#define N_ICMPSTAT	0
#if !defined(hpux) && !defined(solaris2) && !defined(__sgi)
	{ "_icmpstat"},
#else
        { "icmpstat"},
#endif
        { 0 },
};
#endif


#ifdef linux
static void
linux_read_icmp_stat __P((struct icmp_mib *));
#endif

extern int header_icmp __P((struct variable *, oid *, int *, int, int *, int (**write) __P((int, u_char *, u_char, int, u_char*, oid *, int)) ));

	/*********************
	 *
	 *  Initialisation & common implementation functions
	 *
	 *********************/


void	init_icmp( )
{
#if !defined(linux) && !defined(HAVE_SYS_TCPIPSTATS_H)
    init_nlist( icmp_nl );
#endif
}

#define MATCH_FAILED	1
#define MATCH_SUCCEEDED	0

int
header_icmp(vp, name, length, exact, var_len, write_method)
    register struct variable *vp;    /* IN - pointer to variable entry that points here */
    oid     *name;	    /* IN/OUT - input name requested, output name found */
    int     *length;	    /* IN/OUT - length of input and output oid's */
    int     exact;	    /* IN - TRUE if an exact match was requested. */
    int     *var_len;	    /* OUT - length of variable or 0 if function returned. */
    int     (**write_method) __P((int, u_char *, u_char, int, u_char *, oid *, int));
{
#define ICMP_NAME_LENGTH	8
    oid newname[MAX_NAME_LEN];
    int result;
    char c_oid[MAX_NAME_LEN];

    if (snmp_get_do_debugging()) {
      sprint_objid (c_oid, name, *length);
      DEBUGP ("var_icmp: %s %d\n", c_oid, exact);
    }

    bcopy((char *)vp->name, (char *)newname, (int)vp->namelen * sizeof(oid));
    newname[ICMP_NAME_LENGTH] = 0;
    result = compare(name, *length, newname, (int)vp->namelen + 1);
    if ((exact && (result != 0)) || (!exact && (result >= 0)))
        return(MATCH_FAILED);
    bcopy((char *)newname, (char *)name, ((int)vp->namelen + 1) * sizeof(oid));
    *length = vp->namelen + 1;

    *write_method = 0;
    *var_len = sizeof(long);	/* default to 'long' results */
    return(MATCH_SUCCEEDED);
}

	/*********************
	 *
	 *  System specific implementation functions
	 *
	 *********************/

#ifndef solaris2
#ifndef linux
#ifdef HAVE_SYS_TCPIPSTATS_H

u_char *
var_icmp(vp, name, length, exact, var_len, write_method)
    register struct variable *vp;
    oid     *name;
    int     *length;
    int     exact;
    int     *var_len;
    int     (**write_method) __P((int, u_char *, u_char, int, u_char *, oid *, int));
{
    register int i;
    static struct icmpstat icmpstat;
    static struct kna tcpipstats;
    if (header_icmp(vp, name, length, exact, var_len, write_method) == MATCH_FAILED )
	return NULL;

    /*
     *	Get the ICMP statistics from the kernel...
     */
    if (sysmp (MP_SAGET, MPSA_TCPIPSTATS, &tcpipstats, sizeof tcpipstats) == -1) {
	perror ("sysmp(MP_SAGET)(MPSA_TCPIPSTATS)");
    }
#define icmpstat tcpipstats.icmpstat

    switch (vp->magic){
	case ICMPINMSGS:
	    long_return = icmpstat.icps_badcode + icmpstat.icps_tooshort +
			  icmpstat.icps_checksum + icmpstat.icps_badlen;
	    for (i=0; i <= ICMP_MAXTYPE; i++)
		long_return += icmpstat.icps_inhist[i];
	    return (u_char *)&long_return;
	case ICMPINERRORS:
	    long_return = icmpstat.icps_badcode + icmpstat.icps_tooshort +
			  icmpstat.icps_checksum + icmpstat.icps_badlen;
	    return (u_char *)&long_return;
	case ICMPINDESTUNREACHS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_UNREACH];
	case ICMPINTIMEEXCDS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_TIMXCEED];
	case ICMPINPARMPROBS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_PARAMPROB];
	case ICMPINSRCQUENCHS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_SOURCEQUENCH];
	case ICMPINREDIRECTS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_REDIRECT];
	case ICMPINECHOS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_ECHO];
	case ICMPINECHOREPS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_ECHOREPLY];
	case ICMPINTIMESTAMPS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_TSTAMP];
	case ICMPINTIMESTAMPREPS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_TSTAMPREPLY];
	case ICMPINADDRMASKS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_MASKREQ];
	case ICMPINADDRMASKREPS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_MASKREPLY];
	case ICMPOUTMSGS:
	    long_return = icmpstat.icps_oldshort + icmpstat.icps_oldicmp;
	    for (i=0; i <= ICMP_MAXTYPE; i++)
		long_return += icmpstat.icps_outhist[i];
	    return (u_char *)&long_return;
	case ICMPOUTERRORS:
	    long_return = icmpstat.icps_oldshort + icmpstat.icps_oldicmp;
	    return (u_char *)&long_return;
	case ICMPOUTDESTUNREACHS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_UNREACH];
	case ICMPOUTTIMEEXCDS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_TIMXCEED];
	case ICMPOUTPARMPROBS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_PARAMPROB];
	case ICMPOUTSRCQUENCHS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_SOURCEQUENCH];
	case ICMPOUTREDIRECTS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_REDIRECT];
	case ICMPOUTECHOS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_ECHO];
	case ICMPOUTECHOREPS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_ECHOREPLY];
	case ICMPOUTTIMESTAMPS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_TSTAMP];
	case ICMPOUTTIMESTAMPREPS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_TSTAMPREPLY];
	case ICMPOUTADDRMASKS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_MASKREQ];
	case ICMPOUTADDRMASKREPS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_MASKREPLY];
	default:
	    ERROR_MSG("");
    }

    return NULL;
}

#else /* not HAVE_SYS_TCPIPSTATS_H */

u_char *
var_icmp(vp, name, length, exact, var_len, write_method)
    register struct variable *vp;
    oid     *name;
    int     *length;
    int     exact;
    int     *var_len;
    int     (**write_method) __P((int, u_char *, u_char, int, u_char *, oid *, int));
{
    register int i;
    static struct icmpstat icmpstat;

   if (header_icmp(vp, name, length, exact, var_len, write_method) == MATCH_FAILED )
	return NULL;

    /*
     *        Get the ICMP statistics from the kernel...
     */
    KNLookup(icmp_nl, N_ICMPSTAT, (char *)&icmpstat, sizeof (icmpstat));

    switch (vp->magic){
	case ICMPINMSGS:
	    long_return = icmpstat.icps_badcode + icmpstat.icps_tooshort +
			  icmpstat.icps_checksum + icmpstat.icps_badlen;
	    for (i=0; i <= ICMP_MAXTYPE; i++)
		long_return += icmpstat.icps_inhist[i];
	    return (u_char *)&long_return;
	case ICMPINERRORS:
	    long_return = icmpstat.icps_badcode + icmpstat.icps_tooshort +
			  icmpstat.icps_checksum + icmpstat.icps_badlen;
	    return (u_char *)&long_return;
	case ICMPINDESTUNREACHS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_UNREACH];
	case ICMPINTIMEEXCDS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_TIMXCEED];
	case ICMPINPARMPROBS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_PARAMPROB];
	case ICMPINSRCQUENCHS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_SOURCEQUENCH];
	case ICMPINREDIRECTS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_REDIRECT];
	case ICMPINECHOS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_ECHO];
	case ICMPINECHOREPS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_ECHOREPLY];
	case ICMPINTIMESTAMPS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_TSTAMP];
	case ICMPINTIMESTAMPREPS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_TSTAMPREPLY];
	case ICMPINADDRMASKS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_MASKREQ];
	case ICMPINADDRMASKREPS:
	    return (u_char *) &icmpstat.icps_inhist[ICMP_MASKREPLY];
	case ICMPOUTMSGS:
	    long_return = icmpstat.icps_oldshort + icmpstat.icps_oldicmp;
	    for (i=0; i <= ICMP_MAXTYPE; i++)
		long_return += icmpstat.icps_outhist[i];
	    return (u_char *)&long_return;
	case ICMPOUTERRORS:
	    long_return = icmpstat.icps_oldshort + icmpstat.icps_oldicmp;
	    return (u_char *)&long_return;
	case ICMPOUTDESTUNREACHS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_UNREACH];
	case ICMPOUTTIMEEXCDS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_TIMXCEED];
	case ICMPOUTPARMPROBS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_PARAMPROB];
	case ICMPOUTSRCQUENCHS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_SOURCEQUENCH];
	case ICMPOUTREDIRECTS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_REDIRECT];
	case ICMPOUTECHOS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_ECHO];
	case ICMPOUTECHOREPS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_ECHOREPLY];
	case ICMPOUTTIMESTAMPS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_TSTAMP];
	case ICMPOUTTIMESTAMPREPS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_TSTAMPREPLY];
	case ICMPOUTADDRMASKS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_MASKREQ];
	case ICMPOUTADDRMASKREPS:
	    return (u_char *) &icmpstat.icps_outhist[ICMP_MASKREPLY];
	default:
	    ERROR_MSG("");
    }

    return NULL;
}

#endif /* not HAVE_SYS_TCPIPSTATS_H */

#else /* linux */

u_char *
var_icmp(vp, name, length, exact, var_len, write_method)
    register struct variable *vp;
    oid     *name;
    int     *length;
    int     exact;
    int     *var_len;
    int     (**write_method) __P((int, u_char *, u_char, int, u_char *, oid *, int));
{
    static struct icmp_mib icmpstat;

   if (header_icmp(vp, name, length, exact, var_len, write_method) == MATCH_FAILED )
	return(NULL);

    linux_read_icmp_stat (&icmpstat);

    switch (vp->magic){
    case ICMPINMSGS: return (u_char *) &icmpstat.IcmpInMsgs;
    case ICMPINERRORS: return (u_char *) &icmpstat.IcmpInErrors;
    case ICMPINDESTUNREACHS: return (u_char *) &icmpstat.IcmpInDestUnreachs;
    case ICMPINTIMEEXCDS: return (u_char *) &icmpstat.IcmpInTimeExcds;
    case ICMPINPARMPROBS: return (u_char *) &icmpstat.IcmpInParmProbs;
    case ICMPINSRCQUENCHS: return (u_char *) &icmpstat.IcmpInSrcQuenchs;
    case ICMPINREDIRECTS: return (u_char *) &icmpstat.IcmpInRedirects;
    case ICMPINECHOS: return (u_char *) &icmpstat.IcmpInEchos;
    case ICMPINECHOREPS: return (u_char *) &icmpstat.IcmpInEchoReps;
    case ICMPINTIMESTAMPS: return (u_char *) &icmpstat.IcmpInTimestamps;
    case ICMPINTIMESTAMPREPS: return (u_char *) &icmpstat.IcmpInTimestampReps;
    case ICMPINADDRMASKS: return (u_char *) &icmpstat.IcmpInAddrMasks;
    case ICMPINADDRMASKREPS: return (u_char *) &icmpstat.IcmpInAddrMaskReps;
    case ICMPOUTMSGS: return (u_char *) &icmpstat.IcmpOutMsgs;
    case ICMPOUTERRORS: return (u_char *) &icmpstat.IcmpOutErrors;
    case ICMPOUTDESTUNREACHS: return (u_char *) &icmpstat.IcmpOutDestUnreachs;
    case ICMPOUTTIMEEXCDS: return (u_char *) &icmpstat.IcmpOutTimeExcds;
    case ICMPOUTPARMPROBS: return (u_char *) &icmpstat.IcmpOutParmProbs;
    case ICMPOUTSRCQUENCHS: return (u_char *) &icmpstat.IcmpOutSrcQuenchs;
    case ICMPOUTREDIRECTS: return (u_char *) &icmpstat.IcmpOutRedirects;
    case ICMPOUTECHOS: return (u_char *) &icmpstat.IcmpOutEchos;
    case ICMPOUTECHOREPS: return (u_char *) &icmpstat.IcmpOutEchoReps;
    case ICMPOUTTIMESTAMPS: return (u_char *) &icmpstat.IcmpOutTimestamps;
    case ICMPOUTTIMESTAMPREPS: return (u_char *)&icmpstat.IcmpOutTimestampReps;
    case ICMPOUTADDRMASKS: return (u_char *) &icmpstat.IcmpOutAddrMasks;
    case ICMPOUTADDRMASKREPS: return (u_char *) &icmpstat.IcmpOutAddrMaskReps;

    default:
      ERROR_MSG("");
    }
    return NULL;

}

#endif /* linux */
#else /* solaris2 */

u_char *
var_icmp(vp, name, length, exact, var_len, write_method)
    register struct variable *vp;
    oid     *name;
    int     *length;
    int     exact;
    int     *var_len;
    int     (**write_method) __P((int, u_char *, u_char, int, u_char *, oid *, int));
{
    mib2_icmp_t icmpstat;

    if (header_icmp(vp, name, length, exact, var_len, write_method) == MATCH_FAILED )
	return(NULL);

    /*
     *	Get the ICMP statistics from the kernel...
     */
    if (getMibstat(MIB_ICMP, &icmpstat, sizeof(mib2_icmp_t), GET_FIRST, &Get_everything, NULL) < 0)
      return (NULL);		/* Things are ugly ... */

    switch (vp->magic){
	case ICMPINMSGS:
      		long_return = icmpstat.icmpInMsgs;
      		break;
	case ICMPINERRORS:
      		long_return = icmpstat.icmpInErrors;
      		break;
	case ICMPINDESTUNREACHS:
      		long_return = icmpstat.icmpInDestUnreachs;
      		break;
	case ICMPINTIMEEXCDS:
      		long_return = icmpstat.icmpInTimeExcds;
      		break;
	case ICMPINPARMPROBS:
      		long_return = icmpstat.icmpInParmProbs;
      		break;
	case ICMPINSRCQUENCHS:
      		long_return = icmpstat.icmpInSrcQuenchs;
      		break;
	case ICMPINREDIRECTS:
      		long_return = icmpstat.icmpInRedirects;
      		break;
	case ICMPINECHOS:
      		long_return = icmpstat.icmpInEchos;
      		break;
	case ICMPINECHOREPS:
      		long_return = icmpstat.icmpInEchoReps;
      		break;
	case ICMPINTIMESTAMPS:
      		long_return = icmpstat.icmpInTimestamps;
      		break;
	case ICMPINTIMESTAMPREPS:
      		long_return = icmpstat.icmpInTimestampReps;
      		break;
	case ICMPINADDRMASKS:
      		long_return = icmpstat.icmpInAddrMasks;
      		break;
	case ICMPINADDRMASKREPS:
      		long_return = icmpstat.icmpInAddrMaskReps;
      		break;
	case ICMPOUTMSGS:
      		long_return = icmpstat.icmpOutMsgs;
      		break;
	case ICMPOUTERRORS:
      		long_return = icmpstat.icmpOutErrors;
      		break;
	case ICMPOUTDESTUNREACHS:
      		long_return = icmpstat.icmpOutDestUnreachs;
      		break;
	case ICMPOUTTIMEEXCDS:
      		long_return = icmpstat.icmpOutTimeExcds;
      		break;
	case ICMPOUTPARMPROBS:
      		long_return = icmpstat.icmpOutParmProbs;
      		break;
	case ICMPOUTSRCQUENCHS:
      		long_return = icmpstat.icmpOutSrcQuenchs;
      		break;
	case ICMPOUTREDIRECTS:
      		long_return = icmpstat.icmpOutRedirects;
      		break;
	case ICMPOUTECHOS:
      		long_return = icmpstat.icmpOutEchos;
      		break;
	case ICMPOUTECHOREPS:
      		long_return = icmpstat.icmpOutEchoReps;
      		break;
	case ICMPOUTTIMESTAMPS:
      		long_return = icmpstat.icmpOutTimestamps;
      		break;
	case ICMPOUTTIMESTAMPREPS:
      		long_return = icmpstat.icmpOutTimestampReps;
      		break;
	case ICMPOUTADDRMASKS:
      		long_return = icmpstat.icmpOutAddrMasks;
      		break;
	case ICMPOUTADDRMASKREPS:
      		long_return = icmpstat.icmpOutAddrMaskReps;
      		break;
	default:
		ERROR_MSG("");
                return(NULL);
    }
    return ((u_char *) &long_return);
}
#endif /* solaris2 */


	/*********************
	 *
	 *  Internal implementation functions
	 *
	 *********************/


#ifdef linux
/*
 * lucky days. since 1.1.16 the icmp statistics are avail by the proc
 * file-system.
 */

static void
linux_read_icmp_stat (icmpstat)
struct icmp_mib *icmpstat;
{
  FILE *in = fopen ("/proc/net/snmp", "r");
  char line [1024];

  bzero ((char *) icmpstat, sizeof (*icmpstat));

  if (! in)
    return;

  while (line == fgets (line, 1024, in))
    {
      if (26 == sscanf (line,
"Icmp: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
   &icmpstat->IcmpInMsgs, &icmpstat->IcmpInErrors, &icmpstat->IcmpInDestUnreachs, 
   &icmpstat->IcmpInTimeExcds, &icmpstat->IcmpInParmProbs, &icmpstat->IcmpInSrcQuenchs,
   &icmpstat->IcmpInRedirects, &icmpstat->IcmpInEchos, &icmpstat->IcmpInEchoReps, 
   &icmpstat->IcmpInTimestamps, &icmpstat->IcmpInTimestampReps, &icmpstat->IcmpInAddrMasks,
   &icmpstat->IcmpInAddrMaskReps, &icmpstat->IcmpOutMsgs, &icmpstat->IcmpOutErrors,
   &icmpstat->IcmpOutDestUnreachs, &icmpstat->IcmpOutTimeExcds, 
   &icmpstat->IcmpOutParmProbs, &icmpstat->IcmpOutSrcQuenchs, &icmpstat->IcmpOutRedirects,
   &icmpstat->IcmpOutEchos, &icmpstat->IcmpOutEchoReps, &icmpstat->IcmpOutTimestamps, 
   &icmpstat->IcmpOutTimestampReps, &icmpstat->IcmpOutAddrMasks,
   &icmpstat->IcmpOutAddrMaskReps))
	break;
    }
  fclose (in);
}

#endif /* linux */


