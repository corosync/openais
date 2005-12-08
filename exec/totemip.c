/*
 * Copyright (c) 2005 Red Hat Inc
 *
 * All rights reserved.
 *
 * Author: Patrick Caulfield (pcaulfie@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* IPv4/6 abstraction */

#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>

/* ARGH!! I hate netlink */
#include <asm/types.h>
#include <linux/rtnetlink.h>

#include "totemip.h"

#define LOCALHOST_IPV4 "127.0.0.1"
#define LOCALHOST_IPV6 "::1"

#define NETLINK_BUFSIZE 16384

/* Compare two addresses */
int totemip_equal(struct totem_ip_address *addr1, struct totem_ip_address *addr2)
{
	int addrlen = 0;

	if (addr1->family != addr2->family)
		return 0;

	if (addr1->family == AF_INET) {
		addrlen = sizeof(struct in_addr);
	}
	if (addr1->family == AF_INET6) {
		addrlen = sizeof(struct in6_addr);
	}
	assert(addrlen);

	if (memcmp(addr1->addr, addr2->addr, addrlen) == 0)
		return 1;
	else
		return 0;

}

/* Copy a totem_ip_address */
void totemip_copy(struct totem_ip_address *addr1, struct totem_ip_address *addr2)
{
	memcpy(addr1, addr2, sizeof(struct totem_ip_address));
}

/* For sorting etc. params are void * for qsort's benefit */
int totemip_compare(const void *a, const void *b)
{
	int i;
	const struct totem_ip_address *addr1 = a;
	const struct totem_ip_address *addr2 = b;
	struct in6_addr *sin6a;
	struct in6_addr *sin6b;

	if (addr1->family != addr2->family)
		return (addr1->family > addr2->family);

	if (addr1->family == AF_INET) {
		struct in_addr *in1 = (struct in_addr *)addr1->addr;
		struct in_addr *in2 = (struct in_addr *)addr2->addr;

		/* A bit clunky but avoids sign problems */
		if (in1->s_addr == in2->s_addr)
			return 0;
		if (htonl(in1->s_addr) < htonl(in2->s_addr))
			return -1;
		else
			return +1;
	}

	/* Compare IPv6 addresses */
	sin6a = (struct in6_addr *)addr1->addr;
	sin6b = (struct in6_addr *)addr2->addr;

	/* Remember, addresses are in big-endian format.
	   We compare 16bits at a time rather than 32 to avoid sign problems */
	for (i = 0; i < 8; i++) {
		int res = htons(sin6a->s6_addr16[i]) -
			htons(sin6b->s6_addr16[i]);
		if (res) {
			return res;
		}
	}
	return 0;
}

/* Build a localhost totem_ip_address */
int totemip_localhost(int family, struct totem_ip_address *localhost)
{
	char *addr_text;

	if (family == AF_INET)
		addr_text = LOCALHOST_IPV4;
	else
		addr_text = LOCALHOST_IPV6;

	if (inet_pton(family, addr_text, (char *)localhost->addr) <= 0)
		return -1;

	return 0;
}

int totemip_localhost_check(struct totem_ip_address *addr)
{
	struct totem_ip_address localhost;

	if (totemip_localhost(addr->family, &localhost))
		return 0;
	return totemip_equal(addr, &localhost);
}

const char *totemip_print(struct totem_ip_address *addr)
{
	static char buf[INET6_ADDRSTRLEN];

	return inet_ntop(addr->family, addr->addr, buf, sizeof(buf));
}

/* Make a totem_ip_address into a usable sockaddr_storage */
int totemip_totemip_to_sockaddr_convert(struct totem_ip_address *ip_addr,
					uint16_t port, struct sockaddr_storage *saddr, int *addrlen)
{
	int ret = -1;

	if (ip_addr->family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)saddr;

		memset(sin, 0, sizeof(struct sockaddr_in));
		sin->sin_family = ip_addr->family;
		sin->sin_port = port;
		memcpy(&sin->sin_addr, ip_addr->addr, sizeof(struct in_addr));

		*addrlen = sizeof(struct sockaddr_in);
		ret = 0;
	}

	if (ip_addr->family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)saddr;

		memset(sin, 0, sizeof(struct sockaddr_in6));
		sin->sin6_family = ip_addr->family;
		sin->sin6_port = port;
		sin->sin6_scope_id = 2;
		memcpy(&sin->sin6_addr, ip_addr->addr, sizeof(struct in6_addr));

		*addrlen = sizeof(struct sockaddr_in6);
		ret = 0;
	}

	return ret;
}

/* Converts an address string string into a totem_ip_address */
int totemip_parse(struct totem_ip_address *totemip, char *addr)
{
        struct addrinfo *ainfo;
        struct addrinfo ahints;
	struct sockaddr_in *sa;
	struct sockaddr_in6 *sa6;
	int ret;

        memset(&ahints, 0, sizeof(ahints));
        ahints.ai_socktype = SOCK_DGRAM;
        ahints.ai_protocol = IPPROTO_UDP;

        /* Lookup the nodename address */
        ret = getaddrinfo(addr, NULL, &ahints, &ainfo);
	if (ret)
		return -errno;

	sa = (struct sockaddr_in *)ainfo->ai_addr;
	sa6 = (struct sockaddr_in6 *)ainfo->ai_addr;
	totemip->family = ainfo->ai_family;

	if (ainfo->ai_family == AF_INET)
		memcpy(totemip->addr, &sa->sin_addr, sizeof(struct in_addr));
	else
		memcpy(totemip->addr, &sa6->sin6_addr, sizeof(struct in6_addr));

	return 0;
}

/* Make a sockaddr_* into a totem_ip_address */
int totemip_sockaddr_to_totemip_convert(struct sockaddr_storage *saddr, struct totem_ip_address *ip_addr)
{
	int ret = -1;

	ip_addr->family = saddr->ss_family;
	ip_addr->nodeid = 0;

	if (saddr->ss_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)saddr;

		memcpy(ip_addr->addr, &sin->sin_addr, sizeof(struct in_addr));
		ret = 0;
	}

	if (saddr->ss_family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)saddr;

		memcpy(ip_addr->addr, &sin->sin6_addr, sizeof(struct in6_addr));

		ret = 0;
	}
	return ret;
}

int totemip_iface_check(struct totem_ip_address *bindnet,
			struct totem_ip_address *boundto,
			int *interface_up,
			int *interface_num)
{
	int fd;
	struct {
                struct nlmsghdr nlh;
                struct rtgenmsg g;
        } req;
        struct sockaddr_nl nladdr;
	static char rcvbuf[NETLINK_BUFSIZE];

	*interface_up = 0;
	*interface_num = 0;

	/* Ask netlink for a list of interface addresses */
	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd <0)
		return -1;

        setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&rcvbuf,sizeof(rcvbuf));

        memset(&nladdr, 0, sizeof(nladdr));
        nladdr.nl_family = AF_NETLINK;

        req.nlh.nlmsg_len = sizeof(req);
        req.nlh.nlmsg_type = RTM_GETADDR;
        req.nlh.nlmsg_flags = NLM_F_ROOT|NLM_F_MATCH|NLM_F_REQUEST;
        req.nlh.nlmsg_pid = 0;
        req.nlh.nlmsg_seq = 1;
        req.g.rtgen_family = bindnet->family;

        if (sendto(fd, (void *)&req, sizeof(req), 0,
		   (struct sockaddr*)&nladdr, sizeof(nladdr)) < 0)  {
		close(fd);
		return -1;
	}

	/* Look through the return buffer for our address */
	while (1)
	{
		int status;
		struct nlmsghdr *h;
		struct iovec iov = { rcvbuf, sizeof(rcvbuf) };
		struct msghdr msg = {
			(void*)&nladdr, sizeof(nladdr),
			&iov,   1,
			NULL,   0,
			0
		};

		status = recvmsg(fd, &msg, 0);
		if (!status) {
			close(fd);
			return -1;
		}

		h = (struct nlmsghdr *)rcvbuf;
		if (h->nlmsg_type == NLMSG_DONE)
			break;

		if (h->nlmsg_type == NLMSG_ERROR) {
			close(fd);
			return -1;
		}

		while (NLMSG_OK(h, status)) {
			if (h->nlmsg_type == RTM_NEWADDR) {
				struct ifaddrmsg *msg = NLMSG_DATA(h);
				struct rtattr *rta = (struct rtattr *)(msg+1);
				struct totem_ip_address ipaddr;
				unsigned char *data = (unsigned char *)(rta+1);

				ipaddr.family = bindnet->family;
				memcpy(ipaddr.addr, data, TOTEMIP_ADDRLEN);
				if (totemip_equal(&ipaddr, bindnet)) {

					/* Found it - check I/F is UP */
					struct ifreq ifr;
					int ioctl_fd; /* Can't do ioctls on netlink FDs */

					ioctl_fd = socket(AF_INET, SOCK_STREAM, 0);
					if (ioctl_fd < 0) {
						close(fd);
						return -1;
					}
					memset(&ifr, 0, sizeof(ifr));
					ifr.ifr_ifindex = msg->ifa_index;

					/* SIOCGIFFLAGS needs an interface name */
					status = ioctl(ioctl_fd, SIOCGIFNAME, &ifr);
					status = ioctl(ioctl_fd, SIOCGIFFLAGS, &ifr);
					if (status) {
						close(ioctl_fd);
						close(fd);
						return -1;
					}

					if (ifr.ifr_flags & IFF_UP)
						*interface_up = 1;

					*interface_num = msg->ifa_index;
					close(ioctl_fd);
					goto finished;
				}
			}

			h = NLMSG_NEXT(h, status);
		}
	}
finished:
	totemip_copy (boundto, bindnet);
	close(fd);
	return 0;
}