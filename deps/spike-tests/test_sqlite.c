#include <sqlite3.h>
#include <stdio.h>
int main(void) {
    sqlite3 *db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) { printf("open failed\n"); return 1; }
    rc = sqlite3_exec(db, "CREATE TABLE t(a INTEGER, b TEXT); INSERT INTO t VALUES (42, 'hello');", 0, 0, 0);
    if (rc != SQLITE_OK) { printf("exec failed: %s\n", sqlite3_errmsg(db)); return 1; }
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db, "SELECT a, b FROM t", -1, &st, 0);
    if (sqlite3_step(st) == SQLITE_ROW) {
        printf("sqlite OK: a=%d b=%s\n", sqlite3_column_int(st, 0), sqlite3_column_text(st, 1));
    } else {
        printf("sqlite: no row\n");
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return 0;
}
