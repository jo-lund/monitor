#ifndef PACKET_DNS_H
#define PACKET_DNS_H

#include <stdbool.h>
#include <stdint.h>

#define DNS_HDRLEN 12
#define DNS_NAMELEN 254

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

enum dns_section_count {
    QDCOUNT,
    ANCOUNT,
    NSCOUNT,
    ARCOUNT
};

struct application_info;

bool handle_dns(unsigned char *buffer, struct application_info *info);
int parse_dns_name(unsigned char *buffer, unsigned char *ptr, char name[]);
char *get_dns_opcode(uint8_t opcode);
char *get_dns_rcode(uint8_t rcode);
char *get_dns_type(uint16_t type);
char *get_dns_type_extended(uint16_t type);
char *get_dns_class(uint16_t rrclass);
char *get_dns_class_extended(uint16_t rrclass);

#endif
