/******************************************************************************
 * sched - scheduling system
 * Copyright (c) 2014 "Glen Oakley"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/


#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scheduler.h"
#include "telnet.h"

#ifndef PORT
#define PORT 3165
#endif

const char STR_IDPRMPT[] = "Please enter your user id: ";

const char STR_HELP[] = "Welcome to the scheduling system.\n"
  "- h - this help text\n"
  "- l - list the rooms\n"
  "- s ROOM - list the reservations for a room\n"
  "- r ROOM YYYY-MM-DD hh:mm YYYY-MM-DD hh:mm - reserve a room for a specified amount of time (ISO 8601 extended format)\n"
  "- u - list your reservations\n"
  "- d ROOM YYYY-MM-DD hh:mm - delete your reservation that occurs during this time in a room\n"
  "- q - quit\n> ";


/* The callback for the telnet session for each user */
const char *interface(const char *input, void **data)
{
  char state;
  user_t user;
  char *obuf;

  if (!input) {
    if (*data == NULL) {
      // initializing state!
      *data = malloc(sizeof(char) * 4096);
      *((char*)(*data)) = 0;
    } else {
      // closing state!
      free(*data);
      return "GOODBYE!\n";
    }
  }
  // system state
  state = *((char*)(*data));
  memcpy(&user, (*data)+sizeof(char), sizeof(user_t));
  obuf = ((char*)((*data)+sizeof(char)+sizeof(user_t)));
  if (state == 0) {
    *((char*)(*data)) = 1;
    return STR_IDPRMPT;
  }
  if (state == 1) {
    *((char*)(*data)) = 2;
    user = sched_user(atoi(input));
    if (user.id != atoi(input))
      return NULL;
    memcpy((*data)+sizeof(char), &user, sizeof(user_t));
    return STR_HELP;
  }

  if (input[0] == 'q')
    return NULL;
  if (input[0] == 'h')
    return STR_HELP;
  if (input[0] == 'l') {
    size_t cnt;
    size_t i;
    room_t *rooms;
    const char withoutnote[] = "ROOM %4d | %d people (%d sqft)\n";
    const char withnote[] = "ROOM %4d | %d people (%d sqft) (%s)\n";

    cnt = sched_rooms(NULL);
    rooms = malloc(cnt * sizeof(room_t));
    sched_rooms(rooms);
    *obuf = 0;
    for (i = 0; i < cnt; i++)
      sprintf(obuf+strlen(obuf),
              rooms[i].note[0] == 0 ? withoutnote : withnote,
              rooms[i].id,
              rooms[i].capacity,
              rooms[i].sqft,
              rooms[i].note);
    sprintf(obuf+strlen(obuf), "> ");
    return obuf;
  }
  if (input[0] == 'u' || input[0] == 's') {
    ssize_t cnt;
    reservation_t *reservs;
    ssize_t i;

    if (input[0] == 'u') {
      cnt = sched_reservations_user(user.id, NULL);
      reservs = malloc(sizeof(reservation_t) * cnt);
      sched_reservations_user(user.id, reservs);
    }
    if (input[0] == 's') {
      int roomid;
      char *buf = strdup(input+2);
      roomid = atoi(strtok(buf, " \t"));
      cnt = sched_reservations_room(roomid, NULL);
      reservs = malloc(sizeof(reservation_t) * cnt);
      sched_reservations_room(roomid, reservs);
    }
    *obuf = 0;
    for (i = 0; i < cnt; i++) {
      sprintf(obuf+strlen(obuf), "%d - %s", reservs[i].room_id, ctime(&reservs[i].start));
      sprintf(obuf+strlen(obuf)-1, " - %s", ctime(&reservs[i].end));
      sprintf(obuf+strlen(obuf)-1, "\n");
    }
    sprintf(obuf+strlen(obuf), "> ");
    return obuf;
  }
  if (input[0] == 'r') {
    struct tm tm_start, tm_end;
    memset(&tm_start, 0, sizeof(struct tm));
    memset(&tm_end, 0, sizeof(struct tm));
    char *buf = strdup(input+2);
    int roomid = atoi(strtok(buf, " \t"));
    strptime(strtok(NULL, " \t"), "%Y-%m-%d", &tm_start);
    strptime(strtok(NULL, " \t"), "%H:%M", &tm_start);
    strptime(strtok(NULL, " \t"), "%Y-%m-%d", &tm_end);
    strptime(strtok(NULL, " \t"), "%H:%M", &tm_end);
    free(buf);
    reservation_t reservation = { .room_id = roomid,
                                  .user_id = user.id,
                                  .start = mktime(&tm_start),
                                  .end = mktime(&tm_end) };
    if (0 != sched_reserve(reservation, user)) {
      return "NOT OKAY!\n> ";
    } else {
      return "OKAY!\n> ";
    }
  }
  if (input[0] == 'd') {
    struct tm tmtime;
    memset(&tmtime, 0, sizeof(struct tm));
    char *buf = strdup(input+2);
    int roomid = atoi(strtok(buf, " \t"));
    strptime(strtok(NULL, " \t"), "%Y-%m-%d", &tmtime);
    strptime(strtok(NULL, " \t"), "%H:%M", &tmtime);
    free(buf);
    sched_remove(roomid, mktime(&tmtime), mktime(&tmtime), user);
    return "OKAY!\n> ";
  }
  return "UNKNOWN COMMAND!\n";
}


int main(int argc, char **argv)
{
  telnet_t telnet;
  pthread_t *thread;

  if (argc > 1) {
    if (0 != sched_load(argv[1]))
      fprintf(stderr, "COULD NOT LOAD DATABASE %s\n", argv[1]);
  } else {
    if (0 != sched_load("db.db3"))
      fprintf(stderr, "COULD NOT LOAD DATABASE db.db3");
  }

  telnet = telnet_init(PORT);
  assert(0 == telnet_listener(&telnet, interface));
  assert(NULL != (thread = telnet_start(&telnet)));
  pthread_join(*thread, NULL);

  return 0;
}
