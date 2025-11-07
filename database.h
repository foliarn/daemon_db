#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>

// Structure représentant une tâche
typedef struct {
    int id;
    char title[256];
    char description[1024];
    char status[32];
    int priority;
    char created_at[64];
    char updated_at[64];
} Task;

// Fonctions du module database
int db_init(const char *db_path, sqlite3 **db);
int db_create_task(sqlite3 *db, Task *task);
int db_list_tasks(sqlite3 *db, Task **tasks, int *count);
int db_get_task(sqlite3 *db, int id, Task *task);
int db_update_task(sqlite3 *db, int id, const char *title, 
                   const char *description, const char *status, int priority);
int db_delete_task(sqlite3 *db, int id);
int db_close(sqlite3 *db);
void print_error(sqlite3 *db, const char *context);

#endif