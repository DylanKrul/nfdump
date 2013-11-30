/*
 *  This file is part of the nfdump project.
 *
 *  Copyright (c) 2004, SWITCH - Teleinformatikdienste fuer Lehre und Forschung
 *  All rights reserved.
 *  
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions are met:
 *  
 *   * Redistributions of source code must retain the above copyright notice, 
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice, 
 *     this list of conditions and the following disclaimer in the documentation 
 *     and/or other materials provided with the distribution.
 *   * Neither the name of SWITCH nor the names of its contributors may be 
 *     used to endorse or promote products derived from this software without 
 *     specific prior written permission.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 *  POSSIBILITY OF SUCH DAMAGE.
 *  
 *  $Author: haag $
 *
 *  $Id: nftest.c 9 2009-05-07 08:59:31Z haag $
 *
 *  $LastChangedRevision: 9 $
 *	
 */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include "rbtree.h"
#include "nfdump.h"
#include "nftree.h"
#include "nffile.h"
#include "nf_common.h"
#include "util.h"

/* Global Variables */
extern char 	*CurrentIdent;

FilterEngine_data_t	*Engine;

#define ICMP 1
#define TCP	6
#define UDP 17

/* exported fuctions */
void LogError(char *format, ...);

int check_filter_block(char *filter, master_record_t *flow_record, int expect);

void check_offset(char *text, pointer_addr_t offset, pointer_addr_t expect);

void CheckCompression(char *filename);

/* 
 * some modules are needed for daemon code as well as normal stdio code 
 * therefore a generic LogError is defined, which maps in this case
 * to stderr
 */
void LogError(char *format, ...) {
va_list var_args;

	va_start(var_args, format);
	vfprintf(stderr, format, var_args);
	va_end(var_args);

} // End of LogError


int check_filter_block(char *filter, master_record_t *flow_record, int expect) {
int ret, i;
uint64_t	*block = (uint64_t *)flow_record;

	Engine = CompileFilter(filter);
	if ( !Engine ) {
		exit(254);
	}

	Engine->nfrecord = (uint64_t *)flow_record;
	ret =  (*Engine->FilterEngine)(Engine);
	if ( ret == expect ) {
		printf("Success: Startnode: %i Numblocks: %i Extended: %i Filter: '%s'\n", Engine->StartNode, nblocks(), Engine->Extended, filter);
	} else {
		printf("**** FAILED **** Startnode: %i Numblocks: %i Extended: %i Filter: '%s'\n", Engine->StartNode, nblocks(), Engine->Extended, filter);
		DumpList(Engine);
		printf("Expected: %i, Found: %i\n", expect, ret);
		printf("Record:\n");
		for(i=0; i<=10; i++) {
			printf("%3i %.16llx\n", i, (long long)block[i]);
		}
		if ( Engine->IdentList ) {
			printf("Current Ident: %s, Ident 0 %s\n", CurrentIdent, Engine->IdentList[0]);
		}
		exit(255);
	}
	return (ret == expect);
}

void check_offset(char *text, pointer_addr_t offset, pointer_addr_t expect) {

	if ( offset == expect ) {
		printf("Success: %s: %llu\n", text, (unsigned long long)expect);
	} else {
		printf("**** FAILED **** %s expected %llu, evaluated %llu\n", 
			text, (unsigned long long)expect, (unsigned long long)offset);
		// useless to continue
		exit(255);
	}
}

void CheckCompression(char *filename) {
int i, rfd, wfd, compress;
ssize_t	ret;
stat_record_t *stat_ptr;
data_block_header_t *block_header;
char	*string;
char outfile[MAXPATHLEN];
void	*buff, *buff_ptr;

struct timeval  	tstart[2];
struct timeval  	tend[2];
u_long usec, sec;
double wall[2];

	rfd = OpenFile(filename, &stat_ptr, &string);
	if ( rfd < 0 ) {
		fprintf(stderr, "%s\n", string);
		return;
	}
	
	// tmp filename for new output file
	snprintf(outfile, MAXPATHLEN, "%s-tmp", filename);
	outfile[MAXPATHLEN-1] = '\0';

	buff = malloc(BUFFSIZE);
	if ( !buff ) {
		fprintf(stderr, "Buffer allocation error: %s", strerror(errno));
		close(rfd);
		return;
	}
	block_header = (data_block_header_t *)buff;
	buff_ptr	 = (void *)((pointer_addr_t)buff + sizeof(data_block_header_t));

	ret = ReadBlock(rfd, block_header, buff_ptr, &string);
	if ( ret < 0 ) {
		fprintf(stderr, "Error while reading data block. Abort.\n");
		close(rfd);
		unlink(outfile);
		return;
	}
	close(rfd);

	for ( compress=0; compress<=1; compress++ ) {
		wfd = OpenNewFile(outfile, &string, 1);
		if ( wfd < 0 ) {
        	fprintf(stderr, "%s\n", string);
        	return;
    	}

		gettimeofday(&(tstart[compress]), (struct timezone*)NULL);
		for ( i=0; i<100; i++ ) {
			if ( (ret = WriteBlock(wfd, block_header, compress)) <= 0 ) {
				fprintf(stderr, "Failed to write output buffer to disk: '%s'" , strerror(errno));
				close(rfd);
				close(wfd);
				unlink(outfile);
				return;
			}
		}
		gettimeofday(&(tend[compress]), (struct timezone*)NULL);

		CloseUpdateFile(wfd, stat_ptr, 0, "none", compress, &string );
		unlink(outfile);

		if (tend[compress].tv_usec < tstart[compress].tv_usec) 
			tend[compress].tv_usec += 1000000, --tend[compress].tv_sec;

		usec = tend[compress].tv_usec - tstart[compress].tv_usec;
		sec  = tend[compress].tv_sec - tstart[compress].tv_sec;

		wall[compress] = (double)sec + ((double)usec)/1000000;
	}

	printf("100 write cycles, with size %u bytes\n", block_header->size);
	printf("Uncompressed write time: %-.6fs size: %u\n", wall[0], block_header->size);
	printf("Compressed write time  : %-.6fs size: %d\n", wall[1], (int32_t)ret);
	printf("Ratio                  : 1:%-.3f\n", (double)ret/(double)block_header->size);

	if ( wall[0] < wall[1] )
		printf("You should run nfcapd without compression\n");
	else
		printf("You can run nfcapd with compression (-z)\n");
	
} // End of CheckCompression

int main(int argc, char **argv) {
master_record_t flow_record;
uint64_t *blocks, l;
uint32_t size, in[2];
time_t	now;
int ret;
value64_t	v;

    if ( sizeof(struct in_addr) != sizeof(uint32_t) ) {
#ifdef HAVE_SIZE_T_Z_FORMAT
        printf("**** FAILED **** Size struct in_addr %zu != sizeof(uint32_t)\n", sizeof(struct in_addr));
#else 
        printf("**** FAILED **** Size struct in_addr %lu != sizeof(uint32_t)\n", (unsigned long)sizeof(struct in_addr));
#endif
		exit(255);
    }

	l = 0x200000000LL;
	v.val.val64 = l;
	in[0] = v.val.val32[0];
	in[1] = v.val.val32[1];
	ret = memcmp(in, &l, sizeof(uint64_t));
	if ( ret != 0 ) {
        printf("**** FAILED **** val32/64 union check failed!\n" );
		exit(255);
	}

	if ( argc == 2 ) {
		CheckCompression(argv[1]);
		exit(0);
	}


	size = sizeof(common_record_t) - sizeof(uint8_t[4]);
	memset((void *)&flow_record, 0, sizeof(master_record_t));
	blocks = (uint64_t *)&flow_record;

	check_offset("First    Offset", (unsigned int)((pointer_addr_t)&flow_record.first  -  (pointer_addr_t)blocks), BYTE_OFFSET_first);
	check_offset("Common   Offset", (unsigned int)((pointer_addr_t)&flow_record.fill  -  (pointer_addr_t)blocks), size);
	check_offset("Src AS   Offset", (unsigned int)((pointer_addr_t)&flow_record.srcas  -  (pointer_addr_t)&blocks[OffsetAS]), 0);
	check_offset("Dst AS   Offset", (unsigned int)((pointer_addr_t)&flow_record.dstas  -  (pointer_addr_t)&blocks[OffsetAS]), 2);
	check_offset("Src Port Offset", (unsigned int)((pointer_addr_t)&flow_record.srcport - (pointer_addr_t)&blocks[OffsetPort]), 4);
	check_offset("Dst Port Offset", (unsigned int)((pointer_addr_t)&flow_record.dstport - (pointer_addr_t)&blocks[OffsetPort]), 6);
	check_offset("Dir      Offset", (unsigned int)((pointer_addr_t)&flow_record.dir     - (pointer_addr_t)&blocks[OffsetDir]), 4);
	check_offset("Flags    Offset", (unsigned int)((pointer_addr_t)&flow_record.tcp_flags    - (pointer_addr_t)&blocks[OffsetFlags]), 5);
	check_offset("Protocol Offset", (unsigned int)((pointer_addr_t)&flow_record.prot    - (pointer_addr_t)&blocks[OffsetProto]), 6);
	check_offset("tos      Offset", (unsigned int)((pointer_addr_t)&flow_record.tos     - (pointer_addr_t)&blocks[OffsetTos]), 7);

#ifdef HAVE_SIZE_T_Z_FORMAT
	printf("Pointer  Size : %zu\n", sizeof(blocks));
	printf("Time_t   Size : %zu\n", sizeof(now));
	printf("int      Size : %zu\n", sizeof(int));
	printf("long     Size : %zu\n", sizeof(long));
	printf("longlong Size : %zu\n", sizeof(long long));
#else
	printf("Pointer  Size : %lu\n", (unsigned long)sizeof(blocks));
	printf("Time_t   Size : %lu\n", (unsigned long)sizeof(now));
	printf("int      Size : %lu\n", (unsigned long)sizeof(int));
	printf("long     Size : %lu\n", (unsigned long)sizeof(long));
	printf("longlong Size : %lu\n", (unsigned long)sizeof(long long));
#endif

	flow_record.flags	 = 0;
	ret = check_filter_block("ipv4", &flow_record, 1);
	flow_record.flags	 = 2;
	ret = check_filter_block("ipv4", &flow_record, 1);
	ret = check_filter_block("ipv6", &flow_record, 0);
	flow_record.flags	 = 1;
	ret = check_filter_block("ipv4", &flow_record, 0);
	ret = check_filter_block("ipv6", &flow_record, 1);
	flow_record.flags	 = 7;
	ret = check_filter_block("ipv4", &flow_record, 0);
	ret = check_filter_block("ipv6", &flow_record, 1);


	flow_record.prot	 = TCP;
	ret = check_filter_block("any", &flow_record, 1);
	ret = check_filter_block("not any", &flow_record, 0);
	ret = check_filter_block("proto tcp", &flow_record, 1);
	ret = check_filter_block("proto udp", &flow_record, 0);
	flow_record.prot = UDP;
	ret = check_filter_block("proto tcp", &flow_record, 0);
	ret = check_filter_block("proto udp", &flow_record, 1);
	flow_record.prot = 50;
	ret = check_filter_block("proto esp", &flow_record, 1);
	ret = check_filter_block("proto ah", &flow_record, 0);
	flow_record.prot = 51;
	ret = check_filter_block("proto ah", &flow_record, 1);
	flow_record.prot = 46;
	ret = check_filter_block("proto rsvp", &flow_record, 1);
	flow_record.prot = 47;
	ret = check_filter_block("proto gre", &flow_record, 1);
	ret = check_filter_block("proto 47", &flow_record, 1);
	ret = check_filter_block("proto 42", &flow_record, 0);

	flow_record.prot = 1;
	flow_record.dstport = 250; // -> icmp code 250
	ret = check_filter_block("icmp-code 250", &flow_record, 1);
	ret = check_filter_block("icmp-code 251", &flow_record, 0);
	flow_record.dstport = 3 << 8; // -> icmp type 8
	ret = check_filter_block("icmp-type 3", &flow_record, 1);
	ret = check_filter_block("icmp-type 4", &flow_record, 0);


	inet_pton(PF_INET6, "fe80::2110:abcd:1234:5678", flow_record.v6.srcaddr);
	inet_pton(PF_INET6, "fe80::1104:fedc:4321:8765", flow_record.v6.dstaddr);
	flow_record.v6.srcaddr[0] = ntohll(flow_record.v6.srcaddr[0]);
	flow_record.v6.srcaddr[1] = ntohll(flow_record.v6.srcaddr[1]);
	flow_record.v6.dstaddr[0] = ntohll(flow_record.v6.dstaddr[0]);
	flow_record.v6.dstaddr[1] = ntohll(flow_record.v6.dstaddr[1]);
	ret = check_filter_block("src ip fe80::2110:abcd:1234:5678", &flow_record, 1);
	ret = check_filter_block("src ip fe80::2110:abcd:1234:5679", &flow_record, 0);
	ret = check_filter_block("src ip fe80::2111:abcd:1234:5678", &flow_record, 0);
	ret = check_filter_block("dst ip fe80::1104:fedc:4321:8765", &flow_record, 1);
	ret = check_filter_block("dst ip fe80::1104:fedc:4321:8766", &flow_record, 0);
	ret = check_filter_block("dst ip fe80::1105:fedc:4321:8765", &flow_record, 0);
	ret = check_filter_block("ip fe80::2110:abcd:1234:5678", &flow_record, 1);
	ret = check_filter_block("ip fe80::1104:fedc:4321:8765", &flow_record, 1);
	ret = check_filter_block("ip fe80::2110:abcd:1234:5679", &flow_record, 0);
	ret = check_filter_block("ip fe80::1104:fedc:4321:8766", &flow_record, 0);
	ret = check_filter_block("not ip fe80::2110:abcd:1234:5678", &flow_record, 0);
	ret = check_filter_block("not ip fe80::2110:abcd:1234:5679", &flow_record, 1);

	ret = check_filter_block("src ip in [fe80::2110:abcd:1234:5678]", &flow_record, 1);
	ret = check_filter_block("src ip in [fe80::2110:abcd:1234:5679]", &flow_record, 0);

	inet_pton(PF_INET6, "fe80::2110:abcd:1234:0", flow_record.v6.srcaddr);
	flow_record.v6.srcaddr[0] = ntohll(flow_record.v6.srcaddr[0]);
	flow_record.v6.srcaddr[1] = ntohll(flow_record.v6.srcaddr[1]);
	ret = check_filter_block("src net fe80::2110:abcd:1234:0/112", &flow_record, 1);

	inet_pton(PF_INET6, "fe80::2110:abcd:1234:ffff", flow_record.v6.srcaddr);
	flow_record.v6.srcaddr[0] = ntohll(flow_record.v6.srcaddr[0]);
	flow_record.v6.srcaddr[1] = ntohll(flow_record.v6.srcaddr[1]);
	ret = check_filter_block("src net fe80::2110:abcd:1234:0/112", &flow_record, 1);

	inet_pton(PF_INET6, "fe80::2110:abcd:1235:ffff", flow_record.v6.srcaddr);
	flow_record.v6.srcaddr[0] = ntohll(flow_record.v6.srcaddr[0]);
	flow_record.v6.srcaddr[1] = ntohll(flow_record.v6.srcaddr[1]);
	ret = check_filter_block("src net fe80::2110:abcd:1234:0/112", &flow_record, 0);
	ret = check_filter_block("src net fe80::0/16", &flow_record, 1);
	ret = check_filter_block("src net fe81::0/16", &flow_record, 0);

	flow_record.v6.srcaddr[0] = 0;
	flow_record.v6.srcaddr[1] = 0;

	inet_pton(PF_INET6, "fe80::2110:abcd:1234:0", flow_record.v6.dstaddr);
	flow_record.v6.dstaddr[0] = ntohll(flow_record.v6.dstaddr[0]);
	flow_record.v6.dstaddr[1] = ntohll(flow_record.v6.dstaddr[1]);
	ret = check_filter_block("dst net fe80::2110:abcd:1234:0/112", &flow_record, 1);

	inet_pton(PF_INET6, "fe80::2110:abcd:1234:ffff", flow_record.v6.dstaddr);
	flow_record.v6.dstaddr[0] = ntohll(flow_record.v6.dstaddr[0]);
	flow_record.v6.dstaddr[1] = ntohll(flow_record.v6.dstaddr[1]);
	ret = check_filter_block("dst net fe80::2110:abcd:1234:0/112", &flow_record, 1);

	inet_pton(PF_INET6, "fe80::2110:abcd:1235:ffff", flow_record.v6.dstaddr);
	flow_record.v6.dstaddr[0] = ntohll(flow_record.v6.dstaddr[0]);
	flow_record.v6.dstaddr[1] = ntohll(flow_record.v6.dstaddr[1]);
	ret = check_filter_block("dst net fe80::2110:abcd:1234:0/112", &flow_record, 0);
	ret = check_filter_block("dst net fe80::0/16", &flow_record, 1);
	ret = check_filter_block("not dst net fe80::0/16", &flow_record, 0);
	ret = check_filter_block("dst net fe81::0/16", &flow_record, 0);
	ret = check_filter_block("not dst net fe81::0/16", &flow_record, 1);


	/* 172.32.7.16 => 0xac200710
	 * 10.10.10.11 => 0x0a0a0a0b
	 */
	flow_record.v6.srcaddr[0] = 0;
	flow_record.v6.srcaddr[1] = 0;
	flow_record.v6.dstaddr[0] = 0;
	flow_record.v6.dstaddr[1] = 0;
	flow_record.v4.srcaddr = 0xac200710;
	flow_record.v4.dstaddr = 0x0a0a0a0b;
	ret = check_filter_block("src ip 172.32.7.16", &flow_record, 1);
	ret = check_filter_block("src ip 172.32.7.15", &flow_record, 0);
	ret = check_filter_block("dst ip 10.10.10.11", &flow_record, 1);
	ret = check_filter_block("dst ip 10.10.10.10", &flow_record, 0);
	ret = check_filter_block("ip 172.32.7.16", &flow_record, 1);
	ret = check_filter_block("ip 10.10.10.11", &flow_record, 1);
	ret = check_filter_block("ip 172.32.7.17", &flow_record, 0);
	ret = check_filter_block("ip 10.10.10.12", &flow_record, 0);
	ret = check_filter_block("not ip 172.32.7.16", &flow_record, 0);
	ret = check_filter_block("not ip 172.32.7.17", &flow_record, 1);

	ret = check_filter_block("src host 172.32.7.16", &flow_record, 1);
	ret = check_filter_block("src host 172.32.7.15", &flow_record, 0);
	ret = check_filter_block("dst host 10.10.10.11", &flow_record, 1);
	ret = check_filter_block("dst host 10.10.10.10", &flow_record, 0);
	ret = check_filter_block("host 172.32.7.16", &flow_record, 1);
	ret = check_filter_block("host 10.10.10.11", &flow_record, 1);
	ret = check_filter_block("host 172.32.7.17", &flow_record, 0);
	ret = check_filter_block("host 10.10.10.12", &flow_record, 0);
	ret = check_filter_block("not host 172.32.7.16", &flow_record, 0);
	ret = check_filter_block("not host 172.32.7.17", &flow_record, 1);

	ret = check_filter_block("src ip in [172.32.7.16]", &flow_record, 1);
	ret = check_filter_block("src ip in [172.32.7.17]", &flow_record, 0);
	ret = check_filter_block("src ip in [10.10.10.11]", &flow_record, 0);
	ret = check_filter_block("dst ip in [10.10.10.11]", &flow_record, 1);
	ret = check_filter_block("ip in [10.10.10.11]", &flow_record, 1);
	ret = check_filter_block("ip in [172.32.7.16]", &flow_record, 1);
	ret = check_filter_block("src ip in [172.32.7.16 172.32.7.17 10.10.10.11 10.10.10.12 ]", &flow_record, 1);
	ret = check_filter_block("dst ip in [172.32.7.16 172.32.7.17 10.10.10.11 10.10.10.12 ]", &flow_record, 1);
	ret = check_filter_block("ip in [172.32.7.16 172.32.7.17 10.10.10.11 10.10.10.12 ]", &flow_record, 1);
	ret = check_filter_block("ip in [172.32.7.17 172.32.7.18 10.10.10.12 10.10.10.13 ]", &flow_record, 0);

	flow_record.srcport = 63;
	flow_record.dstport = 255;
	ret = check_filter_block("src port 63", &flow_record, 1);
	ret = check_filter_block("dst port 255", &flow_record, 1);
	ret = check_filter_block("port 63", &flow_record, 1);
	ret = check_filter_block("port 255", &flow_record, 1);
	ret = check_filter_block("src port 64", &flow_record, 0);
	ret = check_filter_block("dst port 258", &flow_record, 0);
	ret = check_filter_block("port 64", &flow_record, 0);
	ret = check_filter_block("port 258", &flow_record, 0);

	ret = check_filter_block("src port = 63", &flow_record, 1);
	ret = check_filter_block("src port == 63", &flow_record, 1);
	ret = check_filter_block("src port eq 63", &flow_record, 1);
	ret = check_filter_block("src port > 62", &flow_record, 1);
	ret = check_filter_block("src port gt 62", &flow_record, 1);
	ret = check_filter_block("src port > 63", &flow_record, 0);
	ret = check_filter_block("src port < 64", &flow_record, 1);
	ret = check_filter_block("src port lt 64", &flow_record, 1);
	ret = check_filter_block("src port < 63", &flow_record, 0);

	ret = check_filter_block("dst port = 255", &flow_record, 1);
	ret = check_filter_block("dst port == 255", &flow_record, 1);
	ret = check_filter_block("dst port eq 255", &flow_record, 1);
	ret = check_filter_block("dst port > 254", &flow_record, 1);
	ret = check_filter_block("dst port gt 254", &flow_record, 1);
	ret = check_filter_block("dst port > 255", &flow_record, 0);
	ret = check_filter_block("dst port < 256", &flow_record, 1);
	ret = check_filter_block("dst port lt 256", &flow_record, 1);
	ret = check_filter_block("dst port < 255", &flow_record, 0);

	ret = check_filter_block("src port in [ 62 63 64 ]", &flow_record, 1);
	ret = check_filter_block("src port in [ 62 64 65 ]", &flow_record, 0);
	ret = check_filter_block("dst port in [ 254 255 256 ]", &flow_record, 1);
	ret = check_filter_block("dst port in [ 254 256 257 ]", &flow_record, 0);
	ret = check_filter_block("port in [ 62 63 64 ]", &flow_record, 1);
	ret = check_filter_block("port in [ 254 255 256 ]", &flow_record, 1);
	ret = check_filter_block("port in [ 62 63 64 254 255 256 ]", &flow_record, 1);
	ret = check_filter_block("port in [ 62 63 64 254 256 ]", &flow_record, 1);
	ret = check_filter_block("port in [ 62 64 254 256 ]", &flow_record, 0);
	ret = check_filter_block("not port in [ 62 64 254 256 ]", &flow_record, 1);

	flow_record.srcas = 123;
	flow_record.dstas = 456;
	ret = check_filter_block("src as 123", &flow_record, 1);
	ret = check_filter_block("dst as 456", &flow_record, 1);
	ret = check_filter_block("as 123", &flow_record, 1);
	ret = check_filter_block("as 456", &flow_record, 1);
	ret = check_filter_block("src as 124", &flow_record, 0);
	ret = check_filter_block("dst as 457", &flow_record, 0);
	ret = check_filter_block("as 124", &flow_record, 0);
	ret = check_filter_block("as 457", &flow_record, 0);

	ret = check_filter_block("src as in [ 122 123 124 ]", &flow_record, 1);
	ret = check_filter_block("dst as in [ 122 124 125 ]", &flow_record, 0);
	ret = check_filter_block("dst as in [ 455 456 457 ]", &flow_record, 1);
	ret = check_filter_block("dst as in [ 455 457 458 ]", &flow_record, 0);
	ret = check_filter_block("as in [ 122 123 124 ]", &flow_record, 1);
	ret = check_filter_block("as in [ 455 456 457 ]", &flow_record, 1);
	ret = check_filter_block("as in [ 122 123 124 455 456 457]", &flow_record, 1);
	ret = check_filter_block("as in [ 122 123 124 455 457]", &flow_record, 1);
	ret = check_filter_block("as in [ 122 124 455 456 457]", &flow_record, 1);
	ret = check_filter_block("as in [ 122 124 455 457]", &flow_record, 0);
	ret = check_filter_block("not as in [ 122 124 455 457]", &flow_record, 1);

	ret = check_filter_block("src net 172.32/16", &flow_record, 1);
	ret = check_filter_block("src net 172.32.7/24", &flow_record, 1);
	ret = check_filter_block("src net 172.32.7.0/27", &flow_record, 1);
	ret = check_filter_block("src net 172.32.7.0/28", &flow_record, 0);
	ret = check_filter_block("src net 172.32.7.0 255.255.255.0", &flow_record, 1);
	ret = check_filter_block("src net 172.32.7.0 255.255.255.240", &flow_record, 0);

	ret = check_filter_block("dst net 10.10/16", &flow_record, 1);
	ret = check_filter_block("dst net 10.10.10/24", &flow_record, 1);
	ret = check_filter_block("dst net 10.10.10.0/28", &flow_record, 1);
	ret = check_filter_block("dst net 10.10.10.0/29", &flow_record, 0);
	ret = check_filter_block("dst net 10.10.10.0 255.255.255.240", &flow_record, 1);
	ret = check_filter_block("dst net 10.10.10.0 255.255.255.248", &flow_record, 0);

	ret = check_filter_block("net 172.32/16", &flow_record, 1);
	ret = check_filter_block("net 172.32.7/24", &flow_record, 1);
	ret = check_filter_block("net 172.32.7.0/27", &flow_record, 1);
	ret = check_filter_block("net 172.32.7.0/28", &flow_record, 0);
	ret = check_filter_block("net 172.32.7.0 255.255.255.0", &flow_record, 1);
	ret = check_filter_block("net 172.32.7.0 255.255.255.240", &flow_record, 0);

	ret = check_filter_block("net 10.10/16", &flow_record, 1);
	ret = check_filter_block("net 10.10.10/24", &flow_record, 1);
	ret = check_filter_block("net 10.10.10.0/28", &flow_record, 1);
	ret = check_filter_block("net 10.10.10.0/29", &flow_record, 0);
	ret = check_filter_block("net 10.10.10.0 255.255.255.240", &flow_record, 1);
	ret = check_filter_block("net 10.10.10.0 255.255.255.240", &flow_record, 1);
	ret = check_filter_block("net 10.10.10.0 255.255.255.248", &flow_record, 0);

	ret = check_filter_block("src ip 172.32.7.16 or src ip 172.32.7.15", &flow_record, 1);
	ret = check_filter_block("src ip 172.32.7.15 or src ip 172.32.7.16", &flow_record, 1);
	ret = check_filter_block("src ip 172.32.7.15 or src ip 172.32.7.14", &flow_record, 0);
	ret = check_filter_block("src ip 172.32.7.16 and dst ip 10.10.10.11", &flow_record, 1);
	ret = check_filter_block("src ip 172.32.7.15 and dst ip 10.10.10.11", &flow_record, 0);
	ret = check_filter_block("src ip 172.32.7.16 and dst ip 10.10.10.12", &flow_record, 0);

	flow_record.tcp_flags = 1;
	ret = check_filter_block("flags F", &flow_record, 1);
	ret = check_filter_block("flags S", &flow_record, 0);
	ret = check_filter_block("flags R", &flow_record, 0);
	ret = check_filter_block("flags P", &flow_record, 0);
	ret = check_filter_block("flags A", &flow_record, 0);
	ret = check_filter_block("flags U", &flow_record, 0);
	ret = check_filter_block("flags X", &flow_record, 0);

	flow_record.tcp_flags = 2; // flags S
	ret = check_filter_block("flags S", &flow_record, 1);
	flow_record.tcp_flags = 4;
	ret = check_filter_block("flags R", &flow_record, 1);
	flow_record.tcp_flags = 8;
	ret = check_filter_block("flags P", &flow_record, 1);
	flow_record.tcp_flags = 16;
	ret = check_filter_block("flags A", &flow_record, 1);
	flow_record.tcp_flags = 32;
	ret = check_filter_block("flags U", &flow_record, 1);
	flow_record.tcp_flags = 63;
	ret = check_filter_block("flags X", &flow_record, 1);

	ret = check_filter_block("not flags RF", &flow_record, 0);

	flow_record.tcp_flags = 3;	// flags SF
	ret = check_filter_block("flags SF", &flow_record, 1);
	ret = check_filter_block("flags 3", &flow_record, 1);
	ret = check_filter_block("flags S and not flags AR", &flow_record, 1);
	flow_record.tcp_flags = 7;
	ret = check_filter_block("flags SF", &flow_record, 1);
	ret = check_filter_block("flags R", &flow_record, 1);
	ret = check_filter_block("flags P", &flow_record, 0);
	ret = check_filter_block("flags A", &flow_record, 0);
	ret = check_filter_block("flags = 7 ", &flow_record, 1);
	ret = check_filter_block("flags > 7 ", &flow_record, 0);
	ret = check_filter_block("flags > 6 ", &flow_record, 1);
	ret = check_filter_block("flags < 7 ", &flow_record, 0);
	ret = check_filter_block("flags < 8 ", &flow_record, 1);

	flow_record.tos = 5;
	ret = check_filter_block("tos 5", &flow_record, 1);
	ret = check_filter_block("tos = 5", &flow_record, 1);
	ret = check_filter_block("tos > 5", &flow_record, 0);
	ret = check_filter_block("tos < 5", &flow_record, 0);
	ret = check_filter_block("tos > 4", &flow_record, 1);
	ret = check_filter_block("tos < 6", &flow_record, 1);

	ret = check_filter_block("tos 10", &flow_record, 0);

	flow_record.input = 5;
	ret = check_filter_block("in if 5", &flow_record, 1);
	ret = check_filter_block("in if 6", &flow_record, 0);
	ret = check_filter_block("out if 6", &flow_record, 0);
	flow_record.output = 6;
	ret = check_filter_block("out if 6", &flow_record, 1);

	/* 
	 * 172.32.7.17 => 0xac200711
	 */
	flow_record.dPkts = 1000;
	ret = check_filter_block("packets 1000", &flow_record, 1);
	ret = check_filter_block("packets = 1000", &flow_record, 1);
	ret = check_filter_block("packets 1010", &flow_record, 0);
	ret = check_filter_block("packets < 1010", &flow_record, 1);
	ret = check_filter_block("packets > 110", &flow_record, 1);

	flow_record.dOctets = 2000;
	ret = check_filter_block("bytes 2000", &flow_record, 1);
	ret = check_filter_block("bytes  = 2000", &flow_record, 1);
	ret = check_filter_block("bytes 2010", &flow_record, 0);
	ret = check_filter_block("bytes < 2010", &flow_record, 1);
	ret = check_filter_block("bytes > 210", &flow_record, 1);

	flow_record.dOctets = 2048;
	ret = check_filter_block("bytes 2k", &flow_record, 1);
	ret = check_filter_block("bytes < 2k", &flow_record, 0);
	ret = check_filter_block("bytes > 2k", &flow_record, 0);
	flow_record.dOctets *= 1024;
	ret = check_filter_block("bytes 2m", &flow_record, 1);
	ret = check_filter_block("bytes < 2m", &flow_record, 0);
	ret = check_filter_block("bytes > 2m", &flow_record, 0);
	flow_record.dOctets *= 1024;
	ret = check_filter_block("bytes 2g", &flow_record, 1);
	ret = check_filter_block("bytes < 2g", &flow_record, 0);
	ret = check_filter_block("bytes > 2g", &flow_record, 0);

	/* 
	 * Function tests
	 */
	flow_record.first = 1089534600;		/* 2004-07-11 10:30:00 */
	flow_record.last  = 1089534600;		/* 2004-07-11 10:30:00 */
	flow_record.msec_first = 10;
	flow_record.msec_last  = 20;

	/* duration 10ms */
	ret = check_filter_block("duration == 10", &flow_record, 1);
	ret = check_filter_block("duration < 11", &flow_record, 1);
	ret = check_filter_block("duration > 9", &flow_record, 1);
	ret = check_filter_block("not duration == 10", &flow_record, 0);
	ret = check_filter_block("duration > 10", &flow_record, 0);
	ret = check_filter_block("duration < 10", &flow_record, 0);

	flow_record.first = 1089534600;		/* 2004-07-11 10:30:00 */
	flow_record.last  = 1089534610;		/* 2004-07-11 10:30:10 */
	flow_record.msec_first = 0;
	flow_record.msec_last  = 0;

	/* duration 10s */
	flow_record.dPkts = 1000;
	ret = check_filter_block("duration == 10000", &flow_record, 1);
	ret = check_filter_block("duration < 10001", &flow_record, 1);
	ret = check_filter_block("duration > 9999", &flow_record, 1);
	ret = check_filter_block("not duration == 10000", &flow_record, 0);
	ret = check_filter_block("duration > 10000", &flow_record, 0);
	ret = check_filter_block("duration < 10000", &flow_record, 0);

	ret = check_filter_block("pps == 100", &flow_record, 1);
	ret = check_filter_block("pps < 101", &flow_record, 1);
	ret = check_filter_block("pps > 99", &flow_record, 1);
	ret = check_filter_block("not pps == 100", &flow_record, 0);
	ret = check_filter_block("pps > 100", &flow_record, 0);
	ret = check_filter_block("pps < 100", &flow_record, 0);

	flow_record.dOctets = 1000;
	ret = check_filter_block("bps == 800", &flow_record, 1);
	ret = check_filter_block("bps < 801", &flow_record, 1);
	ret = check_filter_block("bps > 799", &flow_record, 1);
	ret = check_filter_block("not bps == 800", &flow_record, 0);
	ret = check_filter_block("bps > 800", &flow_record, 0);
	ret = check_filter_block("bps < 800", &flow_record, 0);

	flow_record.dOctets = 20000;
	ret = check_filter_block("bps > 1k", &flow_record, 1);
	ret = check_filter_block("bps > 15k", &flow_record, 1);
	ret = check_filter_block("bps > 16k", &flow_record, 0);

	ret = check_filter_block("bpp == 20", &flow_record, 1);
	ret = check_filter_block("bpp < 21", &flow_record, 1);
	ret = check_filter_block("bpp > 19", &flow_record, 1);
	ret = check_filter_block("not bpp == 20", &flow_record, 0);
	ret = check_filter_block("bpp > 20", &flow_record, 0);
	ret = check_filter_block("bpp < 20", &flow_record, 0);

	// ident checks
	CurrentIdent = "channel1";
	ret = check_filter_block("ident channel1", &flow_record, 1);
	ret = check_filter_block("ident channel", &flow_record, 0);
	ret = check_filter_block("ident channel11", &flow_record, 0);
	ret = check_filter_block("not ident channel1", &flow_record, 0);
	ret = check_filter_block("ident none", &flow_record, 0);
	ret = check_filter_block("not ident none", &flow_record, 1);

	return 0;
}
