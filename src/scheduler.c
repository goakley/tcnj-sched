#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <pthread.h>

#include "email.h"
#include "scheduler.h"
#include "sqlite3.h"


static sqlite3 *db = NULL;
static pthread_rwlock_t dblock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t _adminlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t adminlock = PTHREAD_MUTEX_INITIALIZER;
static size_t admin_c = 0;
static pthread_mutex_t _studentlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t studentlock = PTHREAD_MUTEX_INITIALIZER;
static size_t student_c = 0;


static int dbfail()
{
  syslog(LOG_ERR, sqlite3_errmsg(db));
  sqlite3_close(db);
  db = NULL;
  return -1;
}


//static int compar_int_room(const void *a, const void *b)
//{ return (*((int*)a)) - ((room_t*)b)->id; }


static int sql_exec_quiet(const char *sql)
{
  sqlite3_stmt *stmt;
  if (SQLITE_OK != sqlite3_prepare(db, sql, strlen(sql) * sizeof(char),
                                   &stmt, NULL))
    return 1;
  if (SQLITE_DONE != sqlite3_step(stmt))
    return 1;
  sqlite3_finalize(stmt);
  return 0;
}

int sched_load(const char *dbpath)
{
  int status;

  if (SQLITE_OK != sqlite3_open(dbpath, &db))
    return dbfail();
  // ensure the proper tables exist
  status = 0;
  status |= sql_exec_quiet("CREATE TABLE IF NOT EXISTS user ("
                           "id INTEGER PRIMARY KEY,"
                           "status INTEGER NOT NULL,"
                           "email TEXT NOT NULL)");
  status |= sql_exec_quiet("CREATE TABLE IF NOT EXISTS room ("
                           "id INTEGER PRIMARY KEY,"
                           "size INTEGER NOT NULL,"
                           "sqft INTEGER NOT NULL,"
                           "capacity INTEGER NOT NULL,"
                           "note TEXT)");
  status |= sql_exec_quiet("CREATE TABLE IF NOT EXISTS reservation ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "room_id INTEGER NOT NULL,"
                           "user_id INTEGER NOT NULL,"
                           "start_time INTEGER NOT NULL,"
                           "end_time INTEGER NOT NULL)");
  if (status != 0)
    return dbfail();
  // OK
  return 0;
}


user_t sched_user(int id)
{
  char sql[128];
  sqlite3_stmt *stmt;
  user_t user;

  sprintf(sql, "SELECT * FROM user WHERE (id=%d)", id);
  memset(&user, 0, sizeof(user_t));
  user.id = id+1;
  if (SQLITE_OK != sqlite3_prepare(db, sql, strlen(sql) * sizeof(char),
                                   &stmt, NULL)) {
    dbfail();
    return user;
  }
  switch(sqlite3_step(stmt)) {
  case SQLITE_ROW:
    user.id = sqlite3_column_int(stmt, 0);
    user.status = sqlite3_column_int(stmt, 1);
    strncpy(user.email, (const char*)sqlite3_column_text(stmt, 2), 63);
  case SQLITE_DONE:
    sqlite3_finalize(stmt);
    break;
  case SQLITE_ERROR:
    dbfail();
    break;
  default:
    syslog(LOG_ERR, "The SQLITE API is broken");
  }
  return user;
}


room_t sched_room(int id)
{
  char sql[128];
  sqlite3_stmt *stmt;
  room_t room;

  sprintf(sql, "SELECT * FROM room WHERE id=%d", id);
  memset(&room, 0, sizeof(room_t));
  room.id = id+1;
  pthread_rwlock_rdlock(&dblock);
  if (SQLITE_OK != sqlite3_prepare(db, sql, strlen(sql) * sizeof(char),
                                   &stmt, NULL)) {
    dbfail();
    return room;
  }
  switch(sqlite3_step(stmt)) {
  case SQLITE_ROW:
    room.id = sqlite3_column_int(stmt, 0);
    room.size = sqlite3_column_int(stmt, 1);
    room.sqft = sqlite3_column_int(stmt, 2);
    room.capacity = sqlite3_column_int(stmt, 3);
    if (sqlite3_column_text(stmt, 4) != NULL)
      strncpy(room.note, (const char*)sqlite3_column_text(stmt, 4), 63);
    else
      room.note[0] = 0;
  case SQLITE_DONE:
    sqlite3_finalize(stmt);
    break;
  case SQLITE_ERROR:
    dbfail();
    break;
  default:
    syslog(LOG_ERR, "The SQLITE API is broken");
  }
  pthread_rwlock_unlock(&dblock);
  return room;
}


ssize_t sched_rooms(room_t *rooms)
{
  const char sql_count[] = "SELECT COUNT(*) FROM room";
  const char sql_select[] = "SELECT * FROM room ORDER BY id ASC";
  sqlite3_stmt *stmt;
  size_t count;
  int status;

  if (!rooms) {
    if (SQLITE_OK != sqlite3_prepare(db, sql_count,
                                     strlen(sql_count) * sizeof(char),
                                     &stmt, NULL))
      return dbfail();
    if (SQLITE_ROW != sqlite3_step(stmt))
      return dbfail();
    count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
  }
  if (SQLITE_OK != sqlite3_prepare(db, sql_select,
                                   strlen(sql_select) * sizeof(char),
                                   &stmt, NULL))
    return dbfail();
  count = 0;
  while (SQLITE_ROW == (status = sqlite3_step(stmt))) {
    rooms[count].id = sqlite3_column_int(stmt, 0);
    rooms[count].size = sqlite3_column_int(stmt, 1);
    rooms[count].sqft = sqlite3_column_int(stmt, 2);
    rooms[count].capacity = sqlite3_column_int(stmt, 3);
    if (sqlite3_column_text(stmt, 4) != NULL)
      strncpy(rooms[count].note, (const char*)sqlite3_column_text(stmt, 4), 63);
    else
      rooms[count].note[0] = 0;
    count++;
  }
  if (SQLITE_DONE != status)
    return dbfail();
  sqlite3_finalize(stmt);
  return count;
}


ssize_t sched_reservations_room(int room, reservation_t *reservations)
{
  char sql[128];
  sqlite3_stmt *stmt;
  size_t count;
  int status;

  if (!reservations) {
    sprintf(sql, "SELECT COUNT(*) FROM reservation WHERE room_id=%d", room);
    pthread_rwlock_rdlock(&dblock);
    if (SQLITE_OK != sqlite3_prepare(db, sql, strlen(sql) * sizeof(char),
                                     &stmt, NULL))
      goto failure;
    if (SQLITE_ROW != sqlite3_step(stmt))
      goto failure;
    count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    pthread_rwlock_unlock(&dblock);
    return count;
  failure:
    pthread_rwlock_unlock(&dblock);
    return dbfail();
  }
  sprintf(sql, "SELECT * FROM reservation WHERE room_id=%d "
          "ORDER BY start_time ASC", room);
  pthread_rwlock_rdlock(&dblock);
  if (SQLITE_OK != sqlite3_prepare(db, sql, strlen(sql) * sizeof(char),
                                   &stmt, NULL))
    return dbfail();
  count = 0;
  while (SQLITE_ROW == (status = sqlite3_step(stmt))) {
    reservations[count].room_id = sqlite3_column_int(stmt, 1);
    reservations[count].user_id = sqlite3_column_int(stmt, 2);
    reservations[count].start = (time_t)sqlite3_column_int64(stmt, 3);
    reservations[count].end = (time_t)sqlite3_column_int64(stmt, 4);
    reservations[count].next = reservations+(count+1);
    count++;
  }
  reservations[count-1].next = NULL;
  if (SQLITE_DONE != status)
    return dbfail();
  sqlite3_finalize(stmt);
  pthread_rwlock_unlock(&dblock);
  return count;
}


ssize_t sched_reservations_user(int user, reservation_t *reservations)
{
  char sql[128];
  sqlite3_stmt *stmt;
  size_t count;
  int status;

  if (!reservations) {
    sprintf(sql, "SELECT COUNT(*) FROM reservation WHERE user_id=%d", user);
    pthread_rwlock_rdlock(&dblock);
    if (SQLITE_OK != sqlite3_prepare(db, sql, strlen(sql) * sizeof(char),
                                     &stmt, NULL))
      goto failure;
    if (SQLITE_ROW != sqlite3_step(stmt))
      goto failure;
    count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    pthread_rwlock_unlock(&dblock);
    return count;
  failure:
    pthread_rwlock_unlock(&dblock);
    return dbfail();
  }
  sprintf(sql, "SELECT * FROM reservation WHERE user_id=%d "
          "ORDER BY start_time ASC", user);
  pthread_rwlock_rdlock(&dblock);
  if (SQLITE_OK != sqlite3_prepare(db, sql, strlen(sql) * sizeof(char),
                                   &stmt, NULL))
    return dbfail();
  count = 0;
  while (SQLITE_ROW == (status = sqlite3_step(stmt))) {
    reservations[count].room_id = sqlite3_column_int(stmt, 1);
    reservations[count].user_id = sqlite3_column_int(stmt, 2);
    reservations[count].start = (time_t)sqlite3_column_int64(stmt, 3);
    reservations[count].end = (time_t)sqlite3_column_int64(stmt, 4);
    reservations[count].next = reservations+(count+1);
    count++;
  }
  reservations[count-1].next = NULL;
  if (SQLITE_DONE != status)
    return dbfail();
  sqlite3_finalize(stmt);
  pthread_rwlock_unlock(&dblock);
  return count;
}


int sched_reserve(reservation_t reservation, user_t user)
{
  char sql_roomcheck[128];
  reservation_t *query;
  char sql[256];
  int status;
  reservation_t *reservations;
  ssize_t reservation_c;

  switch(user.status) {
  case 2: // admin
    pthread_mutex_lock(&_adminlock);
    if (admin_c == 0)
      pthread_mutex_lock(&adminlock);
    admin_c++;
    pthread_mutex_unlock(&_adminlock);
    break;
  case 0: // student
    pthread_mutex_lock(&adminlock);
    pthread_mutex_lock(&_studentlock);
    if (student_c == 0)
      pthread_mutex_lock(&studentlock);
    student_c++;
    pthread_mutex_unlock(&_studentlock);
    break;
  case 1: // faculty
    pthread_mutex_lock(&adminlock);
    pthread_mutex_lock(&studentlock);
    break;
  }
  sprintf(sql_roomcheck, "SELECT * FROM room WHERE id=%d",
          reservation.room_id);
  if (sched_room(reservation.room_id).id == reservation.room_id) {
    reservation_c = sched_reservations_room(reservation.room_id, NULL);
    reservations = malloc(reservation_c * sizeof(reservation_t));
    sched_reservations_room(reservation.room_id, reservations);
  }
  status = 0;
  // find any value collisions
  for (query = reservations; query; query = query->next) {
    if (reservation.start < query->end && reservation.end > query->start) {
      status = 1;
      break;
    }
  }
  if (!status) {
    pthread_rwlock_wrlock(&dblock);
    // store the new value in the database
    sprintf(sql,
            "INSERT INTO reservation (room_id,user_id,start_time,end_time) "
            "VALUES (%d,%d,%ld,%ld)",
            reservation.room_id,
            reservation.user_id,
            reservation.start,
            reservation.end);
    status = sql_exec_quiet(sql);
    if (status != 0) {
      syslog(LOG_ERR, sqlite3_errmsg(db));
    }
    pthread_rwlock_unlock(&dblock);
  }
  switch(user.status) {
  case 2: // admin
    pthread_mutex_lock(&_adminlock);
    admin_c--;
    if (admin_c == 0)
      pthread_mutex_unlock(&adminlock);
    pthread_mutex_unlock(&_adminlock);
  case 0: // student
    pthread_mutex_lock(&_studentlock);
    student_c--;
    if (student_c == 0)
      pthread_mutex_unlock(&studentlock);
    pthread_mutex_unlock(&_studentlock);
    pthread_mutex_unlock(&adminlock);
    break;
  case 1: // faculty
    pthread_mutex_unlock(&studentlock);
    pthread_mutex_unlock(&adminlock);
    break;
  }
  return status;
}


int sched_remove(int roomid, time_t start, time_t end, user_t user) {
  char sql[256];
  sqlite3_stmt *stmt;
  int status;
  int count;

  sprintf(sql, "SELECT reservation.id, reservation.user_id, user.email "
          "FROM reservation LEFT JOIN user WHERE "
          "reservation.room_id=%d AND reservation.start_time<=%lu AND reservation.end_time>=%lu",
          roomid, start, end);
  pthread_rwlock_rdlock(&dblock);
  if (SQLITE_OK != sqlite3_prepare(db, sql, strlen(sql) * sizeof(char),
                                   &stmt, NULL)) {
    pthread_rwlock_unlock(&dblock);
    return dbfail();
  }
  count = 0;
  while (SQLITE_ROW == (status = sqlite3_step(stmt))) {
    if (user.id != sqlite3_column_int(stmt, 1) && user.status != 2)
      continue;
    email_send((const char*)sqlite3_column_text(stmt, 2),
               "YOUR RESERVATION HAS BEEN MODIFIED");
    sprintf(sql, "DELETE FROM reservation WHERE id=%d",
            sqlite3_column_int(stmt, 0));
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    count++;
  }
  if (SQLITE_DONE != status) {
    pthread_rwlock_unlock(&dblock);
    return dbfail();
  }
  sqlite3_finalize(stmt);
  pthread_rwlock_unlock(&dblock);
  return count;
}
