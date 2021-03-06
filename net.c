/*
Copyright (c) 2007, 2008 by Juliusz Chroboczek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>

#include "babeld.h"
#include "util.h"
#include "net.h"

-		now = time(NULL);
+		clock_gettime(CLOCK_MONOTONIC, &now);
-			if(keyexpires <= now) {
+			if(keyexpires <= now.tv_sec) {

int
babel_socket(int port)
{
    struct sockaddr_in6 sin6;
    int s, rc;
    int saved_errno;
    const int one = 1, zero = 0;
    const int ds = 0xc2;        /* CS6 - Network Control + ecn */

    s = socket(PF_INET6, SOCK_DGRAM, 0);
    if(s < 0)
        return -1;

    rc = setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
    if(rc < 0)
        goto fail;

    rc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if(rc < 0)
        goto fail;

    rc = setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
                    &zero, sizeof(zero));
    if(rc < 0)
        goto fail;

    rc = setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                    &one, sizeof(one));
    if(rc < 0)
        goto fail;

    rc = setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                    &one, sizeof(one));
    if(rc < 0)
        goto fail;

#ifdef IPV6_TCLASS
    rc = setsockopt(s, IPPROTO_IPV6, IPV6_TCLASS, &ds, sizeof(ds));
#else
    rc = -1;
    errno = ENOSYS;
#endif
    if(rc < 0)
        perror("Couldn't set traffic class");

#ifdef IPV6_RECVTCLASS
    rc = setsockopt(s, IPPROTO_IPV6, IPV6_RECVTCLASS, (void *)&one, sizeof option);
#else
    rc = -1;
    errno = ENOSYS;
#endif
    if(rc < 0)
        perror("Couldn't set recv traffic class");
#ifdef IPV6_RECHOPLIMIT    
    rc = setsockopt(nfd, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, (void *)&one, sizeof option);
#else
    rc = -1;
    errno = ENOSYS;
#endif
    if(rc < 0)
        perror("Couldn't set recv hop limit");
    
#ifdef SO_TIMESTAMPNS
    rc = setsockopt(s, SOL_SOCKET, SO_TIMESTAMPNS, (void *)&one, sizeof option);
#else
    rc = -1;
    errno = ENOSYS;
#endif
    if(rc < 0)
        perror("Couldn't set kernel timestamps");

    rc = fcntl(s, F_GETFL, 0);
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_SETFL, (rc | O_NONBLOCK));
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_GETFD, 0);
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_SETFD, rc | FD_CLOEXEC);
    if(rc < 0)
        goto fail;

    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(port);
    rc = bind(s, (struct sockaddr*)&sin6, sizeof(sin6));
    if(rc < 0)
        goto fail;

    return s;

 fail:
    saved_errno = errno;
    close(s);
    errno = saved_errno;
    return -1;
}

/* Tight granularity of timestamps is helpful */

typedef struct packet_data {
  uint8_t tos;
  uint8_t hopcount;
  struct timespec tstamp;
} packet_data_t;


 parse_header(  packet_data_t *pd, *msg) {
  for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
  switch (cmsg->cmsg_level) {
  case SOL_SOCKET:
    switch (cmsg->cmsg_type) {
    case SO_TIMESTAMPNS: {
      struct timespec *stamp =
	(struct timespec *)CMSG_DATA(cmsg);
      logger(LOG_INFO,"SOL_SOCKET SO_TIMESTAMPNS %ld.%09ld",
	     (long)stamp->tv_sec,
	     (long)stamp->tv_nsec);
      break;
    }
    default:
      logger(LOG_ERR,"unrecognised SOL_SOCKET cmsg type %d", cmsg->cmsg_type);
      break;
    }
    break;
  case IPPROTO_IPV6:
    switch (cmsg->cmsg_type) {
    case IPV6_HOPLIMIT:
      pd->ttl = *((int *) CMSG_DATA(cmsg)) & 0xff;
      logger(LOG_INFO,"IPPROTO_IPV6: ttl: %d", *(int *) CMSG_DATA(cmsg));
      break;
    case IPV6_RECVTCLASS:
      pd->tos = (uint8_t*) CMSG_DATA(cmsg);
      logger(LOG_INFO,"IPPROTO_IPV6: tos: %d", pkt.outer_tos);
      break;
    default:
      logger(LOG_ERR,"IPPROTO_IPV6 unrecognised cmsg level %d type %d",
	     cmsg->cmsg_level,
	     cmsg->cmsg_type);
      break;
    }
    
    break;
 }
}
 
int
babel_recv(int s, void *buf, int buflen, struct sockaddr *sin, int slen)
{
    struct iovec iovec[10];
    struct msghdr msg;
    struct cmsghdr *msg;
    char buf[CMSG_SPACE(sizeof(int))];
    int rc;

    memset(&msg, 0, sizeof(msg));
    iovec.iov_base = buf;
    iovec.iov_len = buflen;
    msg.msg_name = sin;
    msg.msg_namelen = slen;
    msg.msg_iov = &iovec;
    msg.msg_iovlen = 4;

    rc = recvmsg(s, &msg, 0);
    if(rc>-1) parse_header(pd,msg);
    return rc;
}

int
babel_send(int s,
           const void *buf1, int buflen1, const void *buf2, int buflen2,
           const struct sockaddr *sin, int slen)
{
    struct iovec iovec[2];
    struct msghdr msg;
    int rc, count = 0;

    iovec[0].iov_base = (void*)buf1;
    iovec[0].iov_len = buflen1;
    iovec[1].iov_base = (void*)buf2;
    iovec[1].iov_len = buflen2;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (struct sockaddr*)sin;
    msg.msg_namelen = slen;
    msg.msg_iov = iovec;
    msg.msg_iovlen = 2;

    /* The Linux kernel can apparently keep returning EAGAIN indefinitely. */

 again:
    rc = sendmsg(s, &msg, 0);
    if(rc < 0) {
        if(errno == EINTR) {
            count++;
            if(count < 100)
                goto again;
        } else if(errno == EAGAIN) {
            int rc2;
            rc2 = wait_for_fd(1, s, 5);
            if(rc2 > 0) {
                count++;
                if(count < 100)
                    goto again;
            }
            errno = EAGAIN;
        }
    }
    return rc;
}

int
tcp_server_socket(int port, int local)
{
    struct sockaddr_in6 sin6;
    int s, rc, saved_errno;
    int one = 1;

    s = socket(PF_INET6, SOCK_STREAM, 0);
    if(s < 0)
        return -1;

    rc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_GETFL, 0);
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_SETFL, (rc | O_NONBLOCK));
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_GETFD, 0);
    if(rc < 0)
        goto fail;

    rc = fcntl(s, F_SETFD, rc | FD_CLOEXEC);
    if(rc < 0)
        goto fail;

    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(port);
    if(local) {
        rc = inet_pton(AF_INET6, "::1", &sin6.sin6_addr);
        if(rc < 0)
            goto fail;
    }
    rc = bind(s, (struct sockaddr*)&sin6, sizeof(sin6));
    if(rc < 0)
        goto fail;

    rc = listen(s, 2);
    if(rc < 0)
        goto fail;

    return s;

 fail:
    saved_errno = errno;
    close(s);
    errno = saved_errno;
    return -1;
}
