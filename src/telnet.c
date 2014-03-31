#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "telnet.h"


telnet_t telnet_init(unsigned short port)
{
  telnet_t telnet;
  int truth = 1;
#ifdef SOCK_NONBLOCK
  assert(0 <= (telnet.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)));
#else
  assert(0 <= (telnet.fd = socket(AF_INET, SOCK_STREAM, 0)));
#endif
  memset(&(telnet.ssocket), 0, sizeof(struct sockaddr_in));
  telnet.ssocket.sin_family = AF_INET;
  telnet.ssocket.sin_addr.s_addr = htonl(INADDR_ANY);
  telnet.ssocket.sin_port = htons(port);
  assert(0 == setsockopt(telnet.fd, SOL_SOCKET, SO_REUSEADDR, &truth, sizeof(int)));
  assert(0 == bind(telnet.fd, (struct sockaddr *)(&(telnet.ssocket)), sizeof(struct sockaddr_in)));
  telnet.listener = NULL;
  return telnet;
}

int telnet_listener(telnet_t *telnet, const char* (*listener)(const char *, void **))
{
  telnet->listener = listener;
  return 0;
}

// BUG: cannot receive more than 512 bytes of data
static void *_telnet_client(void *_)
{
  int fd = ((telnet_t*)_)->fd;
  const char* (*listener)(const char*, void**) = ((telnet_t*)_)->listener;
  char ibuffer[512];
  ssize_t rlen;
  const char *ostring;
  void *data = NULL;
  free(_);
  _ = NULL;

  if ((ostring = listener(NULL, &data)))
    send(fd, ostring, strlen(ostring), 0);
  while(1) {
    rlen = recv(fd, ibuffer, 511, 0);
    if (rlen == 0 || ibuffer[0] < 0 || ibuffer[0] == 4)
      break;
    for (rlen--; ibuffer[rlen] == '\n' || ibuffer[rlen] == '\r'; rlen--);
    ibuffer[++rlen] = 0;
    ostring = listener(ibuffer, &data);
    if (!ostring)
      break;
    send(fd, ostring, strlen(ostring), 0);
  }
  if ((ostring = listener(NULL, &data)))
    send(fd, ostring, strlen(ostring), 0);
  close(fd);
  return NULL;
}

static void *_telnet_main(void *_)
{
  telnet_t *telnet = (telnet_t*)_;
  int cfd;
  telnet_t *ctelnet;
  pthread_t cthread;
  while (1) {
    cfd = accept(telnet->fd, NULL, NULL);
    if (cfd < 0) {
      if (errno == EBADF)
        break;
      continue;
    }
    ctelnet = malloc(sizeof(telnet_t));
    memcpy(ctelnet, telnet, sizeof(telnet_t));
    ctelnet->fd = cfd;
    assert(0 == pthread_create(&cthread, NULL, _telnet_client, ctelnet));
    assert(0 == pthread_detach(cthread));
  }
  return NULL;
}

pthread_t *telnet_start(telnet_t *telnet)
{
  assert(telnet->listener);
  assert(0 == listen(telnet->fd, 1));
  assert(0 == pthread_create(&(telnet->thread), NULL, _telnet_main, telnet));
  //assert(0 == pthread_detach(telnet->thread));
  return &(telnet->thread);
}

int telnet_stop(telnet_t *telnet)
{
  assert(0 == close(telnet->fd));
  return 0;
}
