#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "database.h"

// Fonction pour afficher les erreurs SQLite
void print_error(sqlite3 *db, const char *context) {
    fprintf(stderr, "Erreur [%s]: %s\n", context, sqlite3_errmsg(db));
}

// Init de la db (open)
int db_init(const char *db_path, sqlite3 **db)
{
    int return_code =  sqlite3_open(db_path, db);

    if (return_code != SQLITE_OK) {
        print_error(*db, "ouverture");
        return -1;
    }
    return 0;
}

// Créer la table tasks (seulement si elle n'existe pas)
int db_create_table(sqlite3 *db)
{
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS tasks ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    title TEXT NOT NULL,"
        "    description TEXT,"
        "    status TEXT DEFAULT 'todo',"
        "    priority INTEGER DEFAULT 0,"
        "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erreur création table: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    return 0;
}

// Créer une tâche depuis la struct task
int db_create_task(sqlite3 *db, Task *task)
{
    const char *sql = 
        "INSERT INTO tasks (title, description, status, priority) "
        "VALUES (?, ?, ?, ?);";
    
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        print_error(db, "prepare insert");
        return -1;
    }

    sqlite3_bind_text(stmt, 1, task->title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task->description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, task->status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, task->priority);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        print_error(db, "execute insert");
        sqlite3_finalize(stmt);
        return -1;
    }
    
    int last_id = sqlite3_last_insert_rowid(db);
    printf("Tâche insérée avec ID: %d\n", last_id);
    
    sqlite3_finalize(stmt);
    
    return last_id;
}

// Récupère une tâche et l'ajoute dans la tâche passée en paramètre
int db_get_task(sqlite3 *db, int id, Task *task) 
{
    const char *sql = 
        "SELECT id, title, description, status, priority, "
        "created_at, updated_at FROM tasks WHERE id = ?;";
    
    sqlite3_stmt *stmt;
    int rc;
    
    if (task == NULL) return -1;
    
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        print_error(db, "prepare get task");
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    
    if (rc == SQLITE_ROW) {
        task->id = sqlite3_column_int(stmt, 0);

        strncpy(task->title, (const char *)sqlite3_column_text(stmt, 1), 255);
        task->title[255] = '\0';

        strncpy(task->description, (const char *)sqlite3_column_text(stmt, 2), 1023);
        task->description[1023] = '\0';

        strncpy(task->status, (const char *)sqlite3_column_text(stmt, 3), 31);
        task->status[31] = '\0';

        task->priority = sqlite3_column_int(stmt, 4);

        strncpy(task->created_at, (const char *)sqlite3_column_text(stmt, 5), 63);
        task->created_at[63] = '\0';

        strncpy(task->updated_at, (const char *)sqlite3_column_text(stmt, 6), 63);
        task->updated_at[63] = '\0';
        
        sqlite3_finalize(stmt);
        return 0;

    } else if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -2;  // Introuvable

    } else {
        print_error(db, "step get task");
        sqlite3_finalize(stmt);
        return -1;
    }
}

// Liste toutes les tâches et renvoie un tableau de tâches (utile pour l'affichage général)
int db_list_tasks(sqlite3 *db, Task **tasks, int *count) 
{
    const char *sql = 
        "SELECT id, title, description, status, priority, "
        "created_at, updated_at FROM tasks;";
    
    sqlite3_stmt *stmt;
    int rc;
    
    // Vérifier les paramètres
    if (tasks == NULL || count == NULL) {
        fprintf(stderr, "❌ Erreur: paramètres NULL\n");
        return -1;
    }
    
    *tasks = NULL;
    *count = 0;
    
    // Compter le nombre de tâches pour allouer la mémoire correctement
    const char *count_sql = "SELECT COUNT(*) FROM tasks;";
    rc = sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        print_error(db, "prepare count");
        return -1;
    }
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *count = sqlite3_column_int(stmt, 0);
    } else {
        print_error(db, "step count");
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    
    // Si aucune tâche, retourner succès avec count=0
    if (*count == 0) {
        return 0;
    }
    
    // ===== Étape 2 : Allouer le tableau =====
    *tasks = (Task *)malloc(sizeof(Task) * (*count));
    if (*tasks == NULL) {
        fprintf(stderr, "Erreur: allocation mémoire échouée\n");
        *count = 0;
        return -1;
    }
    
    // ===== Étape 3 : Préparer la requête SELECT =====
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        print_error(db, "prepare select");
        free(*tasks);
        *tasks = NULL;
        *count = 0;
        return -1;
    }
    
    // ===== Étape 4 : Remplir le tableau =====
    int index = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Task *current = &(*tasks)[index];
        
        // ID
        current->id = sqlite3_column_int(stmt, 0);
        
        // Title
        const unsigned char *title = sqlite3_column_text(stmt, 1);
        if (title != NULL) {
            strncpy(current->title, (const char *)title, sizeof(current->title) - 1);
            current->title[sizeof(current->title) - 1] = '\0';
        } else {
            current->title[0] = '\0';
        }
        
        // Description
        const unsigned char *desc = sqlite3_column_text(stmt, 2);
        if (desc != NULL) {
            strncpy(current->description, (const char *)desc, sizeof(current->description) - 1);
            current->description[sizeof(current->description) - 1] = '\0';
        } else {
            current->description[0] = '\0';
        }
        
        // Status
        const unsigned char *status = sqlite3_column_text(stmt, 3);
        if (status != NULL) {
            strncpy(current->status, (const char *)status, sizeof(current->status) - 1);
            current->status[sizeof(current->status) - 1] = '\0';
        } else {
            strcpy(current->status, "todo");
        }
        
        // Priority
        current->priority = sqlite3_column_int(stmt, 4);
        
        // Created_at
        const unsigned char *created = sqlite3_column_text(stmt, 5);
        if (created != NULL) {
            strncpy(current->created_at, (const char *)created, sizeof(current->created_at) - 1);
            current->created_at[sizeof(current->created_at) - 1] = '\0';
        } else {
            current->created_at[0] = '\0';
        }
        
        // Updated_at
        const unsigned char *updated = sqlite3_column_text(stmt, 6);
        if (updated != NULL) {
            strncpy(current->updated_at, (const char *)updated, sizeof(current->updated_at) - 1);
            current->updated_at[sizeof(current->updated_at) - 1] = '\0';
        } else {
            current->updated_at[0] = '\0';
        }
        
        index++;
    }
    
    // Vérifier les erreurs
    if (rc != SQLITE_DONE) {
        print_error(db, "step select");
        free(*tasks);
        *tasks = NULL;
        *count = 0;
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // ===== Étape 5 : Nettoyer =====
    sqlite3_finalize(stmt);
    
    return 0;
}

// Update une task, mettre NULL chaque paramètre que l'on ne veut pas modifier !
int db_update_task(sqlite3 *db, int id, const char *title, const char *description, const char *status, int priority) 
{
    char sql[512] = "UPDATE tasks SET ";
    int first_field = 1;  // Pour gérer les virgules
    int bind_index = 1;   // Index pour les bind
    
    sqlite3_stmt *stmt;
    int rc;
        
    // Ajouter title si non-NULL
    if (title != NULL) {
        if (!first_field) strcat(sql, ", ");
        strcat(sql, "title = ?");
        first_field = 0;
    }
    
    // Ajouter description si non-NULL
    if (description != NULL) {
        if (!first_field) strcat(sql, ", ");
        strcat(sql, "description = ?");
        first_field = 0;
    }
    
    // Ajouter status si non-NULL
    if (status != NULL) {
        if (!first_field) strcat(sql, ", ");
        strcat(sql, "status = ?");
        first_field = 0;
    }
    
    // Ajouter priority si différent de -1 (on utilise -1 comme "ne pas modifier")
    if (priority != -1) {
        if (!first_field) strcat(sql, ", ");
        strcat(sql, "priority = ?");
        first_field = 0;
    }
    
    if (!first_field) strcat(sql, ", ");
    strcat(sql, "updated_at = CURRENT_TIMESTAMP ");
    
    // Ajouter la clause WHERE
    strcat(sql, "WHERE id = ?;");
    
    // Vérifier qu'on a au moins un champ à mettre à jour
    if (first_field) {
        fprintf(stderr, "Erreur: Aucun champ à mettre à jour\n");
        return -1;
    }
    
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        print_error(db, "prepare update");
        return -1;
    }
    
    
    if (title != NULL) {
        sqlite3_bind_text(stmt, bind_index++, title, -1, SQLITE_TRANSIENT);
    }
    
    if (description != NULL) {
        sqlite3_bind_text(stmt, bind_index++, description, -1, SQLITE_TRANSIENT);
    }
    
    if (status != NULL) {
        sqlite3_bind_text(stmt, bind_index++, status, -1, SQLITE_TRANSIENT);
    }
    
    if (priority != -1) {
        sqlite3_bind_int(stmt, bind_index++, priority);
    }
    
    sqlite3_bind_int(stmt, bind_index, id);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        print_error(db, "execute update");
        sqlite3_finalize(stmt);
        return -1;
    }
    
    int changes = sqlite3_changes(db);
    if (changes > 0) {
        printf("Tâche mise à jour (%d ligne(s) modifiée(s))\n", changes);
    } else {
        fprintf(stderr, "Aucune tâche modifiée (ID inexistant ?)\n");
        sqlite3_finalize(stmt);
        return -2;  // ID non trouvé
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

// Delete une tache depuis son id
int db_delete_task(sqlite3 *db, int task_id) {
    const char *sql = "DELETE FROM tasks WHERE id = ?;";
    
    sqlite3_stmt *stmt;
    int rc;
    
    // Préparer
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        print_error(db, "prepare delete");
        return -1;
    }
    
    // Lier l'ID
    sqlite3_bind_int(stmt, 1, task_id);
    
    // Executer
    rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            print_error(db, "execute delete");
            sqlite3_finalize(stmt);
            return -1;
        }
        
        int changes = sqlite3_changes(db);
        sqlite3_finalize(stmt);

        if (changes > 0) {
            printf("Task deleted (%d row(s) affected)\n", changes);
            return 0; // Succès réel
        } else {
            printf("No task deleted (ID not found)\n");
            return -2; // Code pour "Non trouvé"
        }
    }

// Ferme la db
int db_close(sqlite3 *db)
{
    int rc;

    rc = sqlite3_close(db);
    if (rc != SQLITE_OK) {
        print_error(db, "close");
        return -1;
    }

    return 0;
}