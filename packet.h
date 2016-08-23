#ifndef PACKET_H
#define PACKET_H

#include <netinet/in.h>
#include <stdbool.h>
#include "list.h"

/* hardware address length (format aa:bb:cc:dd:ee:ff) */
#define HW_ADDRSTRLEN 18

#define ARP_SIZE 28 /* size of an ARP packet (header + payload) */
#define ETHERNET_HDRLEN 14
#define UDP_HDRLEN 8
#define DNS_HDRLEN 12
#define DNS_NAMELEN 254
#define NBNS_NAMELEN 17
#define MAX_NBNS_NAMES 8
#define MAX_NBNS_ADDR 8

/* DNS opcodes */
#define DNS_QUERY 0  /* standard query */
#define DNS_IQUERY 1 /* inverse query */
#define DNS_STATUS 2 /* server status request */

/* DNS response codes */
#define DNS_NO_ERROR 0         /* no error condition */
#define DNS_FORMAT_ERROR 1     /* name server was unable to interpret the query */
#define DNS_SERVER_FAILURE 2   /* name server was unable to process the query */
#define DNS_NAME_ERROR 3       /* the domain name referenced in the query does not exist */
#define DNS_NOT_IMPLEMENTED 4  /* name server does not support the requested kind of query */
#define DNS_REFUSED 5          /* name server refuses to perform the specified operation */

/* DNS types */
#define DNS_TYPE_A 1       /* a host address */
#define DNS_TYPE_NS 2      /* an authoritative name server */
#define DNS_TYPE_MD 3      /* a mail destination (Obsolete - use MX) */
#define DNS_TYPE_MF 4      /* a mail forwarder (Obsolete - use MX) */
#define DNS_TYPE_CNAME 5   /* the canonical name for an alias */
#define DNS_TYPE_SOA 6     /* marks the start of a zone of authority */
#define DNS_TYPE_MB 7      /* a mailbox domain name (EXPERIMENTAL) */
#define DNS_TYPE_MG 8      /* a mail group member (EXPERIMENTAL) */
#define DNS_TYPE_MR 9      /* a mail rename domain name (EXPERIMENTAL) */
#define DNS_TYPE_NULL 10   /* a null RR (EXPERIMENTAL) */
#define DNS_TYPE_WKS 11    /* a well known service description */
#define DNS_TYPE_PTR 12    /* a domain name pointer */
#define DNS_TYPE_HINFO 13  /* host information */
#define DNS_TYPE_MINFO 14  /* mailbox or mail list information */
#define DNS_TYPE_MX 15     /* mail exchange */
#define DNS_TYPE_TXT 16    /* text strings */
#define DNS_TYPE_AAAA 28   /* a host IPv6 address */
#define DNS_QTYPE_AXFR 252   /* a request for a transfer of an entire zone */
#define DNS_QTYPE_MAILB 253  /* a request for mailbox-related records (MB, MG or MR) */
#define DNS_QTYPE_MAILA 254  /* a request for mail agent RRs (Obsolete - see MX) */
#define DNS_QTYPE_STAR 255   /* a request for all records */

/* DNS classes */
#define DNS_CLASS_IN 1      /* the Internet */
#define DNS_CLASS_CS 2      /* the CSNET class (Obsolete - used only for examples in
                               obsolete RFCs) */
#define DNS_CLASS_CH 3      /* the CHAOS class */
#define DNS_CLASS_HS 4      /* Hesiod */
#define DNS_QCLASS_STAR 255 /* any class */

/* NBNS opcodes */
#define NBNS_QUERY 0
#define NBNS_REGISTRATION 5
#define NBNS_RELEASE 6
#define NBNS_WACK 7
#define NBNS_REFRESH 8

/* NBNS response codes */
#define NBNS_NO_ERROR 0
#define NBNS_FMT_ERR 0x1 /* Format Error. Request was invalidly formatted */
#define NBNS_SRV_ERR 0x2 /* Server failure. Problem with NBNS, cannot process name */
#define NBNS_IMP_ERR 0x4 /* Unsupported request error. Allowable only for challenging 
                            NBNS when gets an Update type registration request */
#define NBNS_RFS_ERR 0x5 /* Refused error. For policy reasons server will not
                            register this name from this host */
#define NBNS_ACT_ERR 0x6 /* Active error. Name is owned by another node */
#define NBNS_CFT_ERR 0x7 /* Name in conflict error. A UNIQUE name is owned by more
                            than one node */

/* NBNS types */
#define NBNS_A 0X0001 /* IP address Resource Record */
#define NBNS_NS 0x0002 /* Name Server Resource Record */
#define NBNS_NULL 0x000A /* NULL Resource Record */
#define NBNS_NB 0x0020 /* NetBIOS general Name Service Resource Record */
#define NBNS_NBSTAT 0x0021 /* NetBIOS NODE STATUS Resource Record */

/* NBNS class */
#define NBNS_IN 0x0001 /* Internet class */

/* NBNS owner node type */
#define NBNS_BNODE 0
#define NBNS_PNODE 1
#define NBNS_MNODE 2

enum port {
    DNS = 53,   /* Domain Name Service */
    HTTP = 80,  /* Hypertext Transfer Protocol */
    /*
     * NetBIOS is used for SMB/CIFS-based Windows file sharing. SMB can now run
     * Directly over TCP port 445, so NetBIOS is used for legacy support. Newer
     * Windows system can use DNS for all the purposes for which NBNS was used
     * previously.
     */
    NBNS = 137, /* NetBIOS Name Service */
    NBDS = 138, /* NetBIOS Datagram Service */
    NBSS = 139, /* NetBIOS Session Service */
    SSDP = 1900 /* Simple Service Discovery Protocol */
};

enum packet_type {
    UNKNOWN = -1,
    ETHERNET
};

enum eth_encapsulation {
    ETH_II,
    ETH_802_3
};

enum dns_section_count {
    QDCOUNT,
    ANCOUNT,
    NSCOUNT,
    ARCOUNT
};

struct dns_info {
    uint16_t id; /* A 16 bit identifier */
    unsigned int qr     : 1; /* 0 DNS query, 1 DNS response */
    unsigned int opcode : 4; /* specifies the kind of query in the message */
    unsigned int aa     : 1; /* authoritative answer */
    unsigned int tc     : 1; /* truncation - specifies that the message was truncated */
    unsigned int rd     : 1; /* recursion desired - if set it directs the name server
                                to pursue the query recursively */
    unsigned int ra     : 1; /* recursion avilable - denotes whether recursive query
                                support is available in the name server */
    unsigned int rcode  : 4; /* response code */
    unsigned int section_count[4];

    /* question section */
    struct {
        char qname[DNS_NAMELEN];
        uint16_t qtype;  /* QTYPES are a superset of TYPES */
        uint16_t qclass; /* QCLASS values are a superset of CLASS values */
    } question;

    /* answer section */
    struct dns_resource_record {
        /* a domain name to which the resource record pertains */
        char name[DNS_NAMELEN];
        uint16_t type;
        uint16_t rrclass;
        /*
         * Specifies the time interval (in seconds) that the resource record
         * may be cached before it should be discarded. Zero values are
         * interpreted to mean that the RR can only be used for the
         * transaction in progress, and should not be cached. */
        uint32_t ttl;
        /*
         * The format of rdata varies according to the type and class of the
         * resource record.
         */
        union {
            /* a domain name which specifies the canonical or primary name
               for the owner. The owner name is an alias. */
            char cname[DNS_NAMELEN];
            /* a domain name which points to some location in the domain
               name space. */
            char ptrdname[DNS_NAMELEN];
            /* a domain name which specifies a host which should be
               authoritative for the specified class and domain */
            char nsdname[DNS_NAMELEN];
            uint32_t address; /* a 32 bit IPv4 internet address */

             /* zone of authority */
            struct {
                /* the domain name of the name server that was the
                   original or primary source of data for this zone */
                char mname[DNS_NAMELEN];
                /* a domain name which specifies the mailbox of the
                   person responsible for this zone */
                char rname[DNS_NAMELEN];
                uint32_t serial; /* the version number of the original copy of the zone */
                /* all times are in units of seconds */
                int32_t refresh; /* time interval before the zone should be refreshed */
                int32_t retry; /* time interval that should elapse before a
                                  failed refresh should be retried */
                int32_t expire; /* time value that specifies the upper limit on
                                   the time interval that can elapse before the
                                   zone is no longer authoritative */
                uint32_t minimum; /* the minimum ttl field that should be exported
                                     with any RR from this zone */
            } soa;

            uint8_t ipv6addr[16];
        } rdata;
    } *record;
};

struct nbns_info {
    uint16_t id; /* transaction ID */
    unsigned int r      : 1; /* 0 request, 1 response */
    unsigned int opcode : 4; /* packet type code */
    unsigned int aa     : 1; /* authoritative answer */
    unsigned int tc     : 1; /* truncation */
    unsigned int rd     : 1; /* recursion desired */
    unsigned int ra     : 1; /* recursion avilable */
    unsigned int broadcast : 1; /* 1 broadcast or multicast, 0 unicast */
    unsigned int rcode  : 4;
    unsigned int section_count[4];

    /* question section */
    struct {
        /* the compressed name representation of the NetBIOS name for the request */
        char qname[NBNS_NAMELEN];
        uint16_t qtype;  /* the type of request */
        uint16_t qclass; /* the class of the request */
    } question;

    /* answer/additional records section */
    struct nbns_rr {
        /* the compressed name representation of the NetBIOS name corresponding
           to this resource record */
        char rrname[NBNS_NAMELEN];
        uint16_t rrtype; /* resource record type code */
        uint16_t rrclass; /* resource record class code */
        uint32_t ttl; /* the Time To Live of the resource record's name */

        /* rrtype and rrclass dependent field */
        union {
            struct {
                /*
                 * group name flag.
                 * 1 rrname is a group NetBIOS name
                 * 0 rrname is a unique NetBIOS name
                 */
                unsigned int g : 1;
                /*
                 * Owner Node Type:
                 *    00 = B node
                 *    01 = P node
                 *    10 = M node
                 *    11 = Reserved for future use
                 * For registration requests this is the claimant's type.
                 * For responses this is the actual owner's type.
                 */
                unsigned int ont : 2;
                uint8_t num_addr;
                uint32_t address[MAX_NBNS_ADDR]; /* IP address[es] of the name's owner */
            } nb;
            struct {
                char node_name[NBNS_NAMELEN];
                uint16_t name_flags;
            } nbstat[MAX_NBNS_NAMES];
            char nsdname[NBNS_NAMELEN];
            uint32_t nsdipaddr;
        } rdata;
    } *record;
};

struct http_info {
    char *start_line;
    list_t *header;
    char *data;
    unsigned int len;
};

struct application_info {
    uint16_t utype; /* specifies the application layer protocol */
    union {
        struct dns_info *dns;
        struct nbns_info *nbns;
        struct http_info *http;
        list_t *ssdp;
    };
};

// TODO: Improve the structure of this
struct ip_info {
    unsigned int version : 4;
    unsigned int ihl     : 4;
    unsigned int dscp    : 6; /* Differentiated Services Code Point (RFC 2474) */
    unsigned int ecn     : 2; /* Explicit congestion notification (RFC 3168) */
    uint16_t length; /* The entire packet size in bytes, including header and data */
    uint16_t id; /* Identification field, used for uniquely identifying group of fragments */
    uint16_t foffset; /* Fragment offset */
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    char src[INET_ADDRSTRLEN];
    char dst[INET_ADDRSTRLEN];
    union {
        struct {
            uint16_t src_port;
            uint16_t dst_port;
            uint16_t len;
            uint16_t checksum;
            struct application_info data;
        } udp;
        struct {
            uint16_t src_port;
            uint16_t dst_port;
            uint32_t seq_num;
            uint32_t ack_num;
            unsigned int offset : 4;
            unsigned int ns  : 1;
            unsigned int cwr : 1;
            unsigned int ece : 1;
            unsigned int urg : 1;
            unsigned int ack : 1;
            unsigned int psh : 1;
            unsigned int rst : 1;
            unsigned int syn : 1;
            unsigned int fin : 1;
            uint16_t window;
            uint16_t checksum;
            uint16_t urg_ptr;
            unsigned char *options;
            struct application_info data;
        } tcp;
        struct {
            uint8_t type;
            uint8_t max_resp_time;
            uint16_t checksum;
            char group_addr[INET_ADDRSTRLEN];
        } igmp;
        struct {
            uint8_t type;
            uint8_t code;
            uint16_t checksum;
            union {
                struct { /* used in echo request/reply messages */
                    uint16_t id;
                    uint16_t seq_num;
                } echo;
                uint32_t gateway; /* gateway address, used in redirect messages */
            };
        } icmp;
    };
};

struct arp_info {
    char sip[INET_ADDRSTRLEN];   /* sender IP address */
    char tip[INET_ADDRSTRLEN];   /* target IP address */
    char sha[HW_ADDRSTRLEN];     /* sender hardware address */
    char tha[HW_ADDRSTRLEN];     /* target hardware address */
    uint16_t ht;                 /* hardware type, e.g. Ethernet, Amateur radio */
    uint16_t pt;                 /* protocol type, IPv4 is 0x0800 */
    uint8_t hs;                  /* hardware size */
    uint8_t ps;                  /* protocol size */
    uint16_t op;                 /* ARP opcode */
};

struct eth_info {
    unsigned char mac_src[ETH_ALEN];
    unsigned char mac_dst[ETH_ALEN];
    uint16_t ethertype;
    enum eth_encapsulation link;
    union {
        struct arp_info *arp;
        struct ip_info *ip;
    };
};

struct eth_802_2_llc {
    uint8_t dsap; /* destination service access point */
    uint8_t ssap; /* source service access point */
    uint8_t control; /* possible to be 2 bytes? */
    struct {
        unsigned char oui[3]; /* IEEE Organizationally Unique Identifier */
        uint16_t protocol_id; /* If OUI is 0 the protocol ID is the Ethernet type */
    } snap;
};

/*
 * Generic packet structure that can be used for every type of packet. For now
 * only support for Ethernet.
 */
struct packet {
    enum packet_type ptype;
    struct eth_info eth;
};

/*
 * Get a packet from the network interface card. Will allocate enough memory
 * for packet, which needs to be freed with free_packet.
 */
size_t read_packet(int sockfd, unsigned char *buffer, size_t n, struct packet **p);

/* Free the memory allocated for packet */
void free_packet(void *packet);

#endif
