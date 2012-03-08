#include "fp_db.h"
#include "fpserv_async.h"

// These should be in a config file somewhere. Probably.
const char* SQLITE_DB = "fingerprints.db";

DB::DB() {
  db = NULL;

  sqlite3_open(SQLITE_DB, &db);
}

DB::~DB() {
  sqlite3_close(db);
}

bool DB::SaveUser(User* u) {
  if(!u)
    return false;
  int ret;

  sqlite3_stmt* statement = NULL;

  if( SQLITE_OK !=  sqlite3_prepare(db,
      "insert into fingerprints VALUES(?,?);",
      -1,
      &statement,
      0)) {
    return false;
  }

  size_t size = 0;
  unsigned char* buffer = NULL;
  size = fp_print_data_get_data(u->print, &buffer);

  if(size <= 0)
    return false;

  ret = sqlite3_bind_text(statement, 1, u->username.c_str(), u->username.length(), SQLITE_TRANSIENT);
  if(ret != SQLITE_OK) {
    return false;
  }

  // This assumes that a char is one byte. Fix if you care.
  // Also, tell Sqlite to make a copy of the data, so we can free ours.
  ret = sqlite3_bind_blob(statement, 2, buffer, size, SQLITE_TRANSIENT);
  if(ret != SQLITE_OK) {
    return false;
  }

  ret = sqlite3_step(statement);
  if(ret != SQLITE_DONE) {
    return false;
  }

  if(buffer)
    free(buffer);

  return true;
}

std::vector<User*> DB::GetUsers() {
  std::vector<User*> v;
  return v;
}

