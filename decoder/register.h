#ifndef DECODER_REGISTER
#define DECODER_REGISTER

#include "decoder.h"

typedef void (*register_function)(void);

static register_function decoder_functions[] = {
    register_ethernet,
    register_llc,
    register_stp,
    register_snap,
    register_arp,
    register_ip,
    register_ipv6,
    register_icmp,
    register_icmp6,
    register_igmp,
    register_pim,
    register_udp,
    register_tcp,
    register_dns,
    register_nbns,
    register_nbds,
    register_http,
    register_imap,
    register_snmp,
    register_ssdp,
    register_tls,
    register_smb,
    register_dhcp,
    register_smtp
};

#endif
