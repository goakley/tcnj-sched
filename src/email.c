#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "email.h"

// no error checking really >~>
int email_send(const char *address, const char *body)
{
  int fd;
  int truth = 1;
  struct sockaddr_in ssocket;
  assert(0 <= (fd = socket(AF_INET, SOCK_STREAM, 0)));
  memset(&ssocket, 0, sizeof(struct sockaddr_in));
  ssocket.sin_family = AF_INET;
  ssocket.sin_port = htons(EMAIL_SERVER_PORT);
  if (1 != inet_pton(AF_INET, EMAIL_SERVER_ADDR, &ssocket.sin_addr))
    return -1;
  assert(0 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &truth, sizeof(int)));
  if (0 != connect(fd, (struct sockaddr *)&ssocket, sizeof(ssocket)))
    return -1;

  char *message = malloc((16 + strlen(address) + strlen(body)) * sizeof(char));
  sprintf(message, "HELO localhost\r\n");
  assert(0 <= send(fd, message, strlen(message), 0));
  sprintf(message, "MAIL FROM: %s\r\n", EMAIL_ADDR_ADMIN);
  assert(0 <= send(fd, message, strlen(message), 0));
  sprintf(message, "RCPT TO: %s\r\n", address);
  assert(0 <= send(fd, message, strlen(message), 0));
  sprintf(message, "DATA\r\n");
  assert(0 <= send(fd, message, strlen(message), 0));
  sprintf(message, "%s\r\n.\r\n", body);
  assert(0 <= send(fd, message, strlen(message), 0));
  sprintf(message, "QUIT\r\n");
  assert(0 <= send(fd, message, strlen(message), 0));
  close(fd);
  return 0;
}
