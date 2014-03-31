#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <time.h>


typedef struct user_s {
  int id;
  int status;
  char email[64];
} user_t;

typedef struct reservation_s {
  struct reservation_s *next;
  int room_id;
  int user_id;
  time_t start;
  time_t end;
} reservation_t;

typedef struct room_s {
  int id;
  int size;
  int sqft;
  int capacity;
  char note[128];
} room_t;


/**
 * @brief Initializes the scheduling system by loading from the database
 * @param dbpath The file path to the SQLITE3 database
 * @return 0 on success
 */
int sched_load(const char *dbpath);

/**
 */
user_t sched_user(int id);

/**
 */
room_t sched_room(int id);

/**
 * @brief Fill an array with all the rooms in the system
 * If the rooms pointer is NULL, the return value will still be sane.
 * @param An array of size >= the return value of this function
 * @return The number of rooms in the system
 */
ssize_t sched_rooms(room_t *rooms);

ssize_t sched_reservations_room(int room, reservation_t *reservations);

ssize_t sched_reservations_user(int user, reservation_t *reservations);

/**
 * @brief Attempts to place a reservation into the system
 * @return 0 if the reservation was added successfully, otherwise non-zero
 */
int sched_reserve(reservation_t reservation, user_t user);

/**
 * @brief Attempts to remote room reservations that occupy a time block
 * Rooms will only be removed if owned by the user or
 * all will be removed if the user is an admin.
 * @return The number of removed reservations
 */
int sched_remove(int roomid, time_t start, time_t end, user_t user);


#endif
