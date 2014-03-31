#ifndef TELNET_H
#define TELNET_H

#include <pthread.h>
#include <arpa/inet.h>


typedef struct telnet_s {
  int fd;
  struct sockaddr_in ssocket;
  const char* (*listener)(const char*, void **);
  pthread_t thread;
} telnet_t;


/**
 * @brief Creates a new telnet identifier listening on the specified port
 */
telnet_t telnet_init(unsigned short port);

/**
 * @brief Sets the listening function for a telnet identifier
 * The listening function will be called whenever a client connects,
 * disconnects, or sends a line of input.
 * The input from the client will be passed to the function (NULL will be
 * passed if the event is a connect/disconnect).
 * The second argument to the listener is available for tracking state.
 * It refers to a location in memory that is initially NULL and can be set by
 * the listener funciton to hold arbitrary data that is maintained throughout
 * the calls to the listener.
 * The listener function should return a valid string that will be returned to
 * the client.
 * Returning NULL will close the connection with the client, however the
 * listener function will be called one more time to handle the disconnect
 * (input will be NULL).
 * This function should be called before `telnet_start`.
 */
int telnet_listener(telnet_t *, const char* (*listener)(const char*, void**));

/**
 * @brief Begins listening for incoming connections.
 */
pthread_t *telnet_start(telnet_t *);

/**
 * @brief Stops listening for incoming connections.
 * Any currently established connections will not be closed.
 */
int telnet_stop(telnet_t *);

#endif
