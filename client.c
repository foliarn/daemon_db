#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "protocol.h"

#define SEM_UI_NAME "/task_client_ui_sem"

// Helper function to connect to the daemon
int connect_to_daemon() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect"); 
        close(fd);
        return -1;
    }

    return fd;
}

// Command: list
void cmd_list() {
    int fd = connect_to_daemon();
    if (fd == -1) return;

    Message req;
    memset(&req, 0, sizeof(req));
    req.type = REQ_LIST;

    if (send_message(fd, &req) == -1) {
        perror("send");
        close(fd);
        return;
    }

    Message resp;
    if (recv_message(fd, &resp) == -1) {
        perror("recv");
        close(fd);
        return;
    }

    if (resp.status == RESP_SUCCESS) {
        int count = resp.count;
        printf("[INFO] Task List (%d tasks):\n", count);
        // Ajout de la colonne Prio
        printf("%-4s | %-4s | %-20s | %-10s | %s\n", "ID", "Prio", "Title", "Status", "Description");
        printf("----------------------------------------------------------------------\n");

        for (int i = 0; i < count; i++) {
            Message task_msg;
            if (recv_message(fd, &task_msg) == -1) break;
            
            // Affichage de la priorité
            printf("%-4d | %-4d | %-20s | %-10s | %s\n", 
                   task_msg.task_data.id, 
                   task_msg.task_data.priority,
                   task_msg.task_data.title, 
                   task_msg.task_data.status,
                   task_msg.task_data.description);
        }
    } else {
        printf("[ERROR] Server error listing tasks.\n");
    }

    close(fd);
}

// Command: add
void cmd_add(char *title, char *description, int priority) {
    int fd = connect_to_daemon();
    if (fd == -1) return;

    Message req;
    memset(&req, 0, sizeof(req));
    req.type = REQ_CREATE;
    
    strncpy(req.task_data.title, title, sizeof(req.task_data.title) - 1);
    if (description) {
        strncpy(req.task_data.description, description, sizeof(req.task_data.description) - 1);
    }
    strcpy(req.task_data.status, "todo");
    req.task_data.priority = priority;

    if (send_message(fd, &req) == -1) {
        perror("send");
        close(fd);
        return;
    }

    Message resp;
    if (recv_message(fd, &resp) == 0 && resp.status == RESP_SUCCESS) {
        printf("[SUCCESS] Task created with ID: %d\n", resp.task_id);
    } else {
        printf("[ERROR] Creating task.\n");
    }

    close(fd);
}

// Command: delete
void cmd_delete(int id) {
    int fd = connect_to_daemon();
    if (fd == -1) return;

    Message req;
    memset(&req, 0, sizeof(req));
    req.type = REQ_DELETE;
    req.task_id = id;

    send_message(fd, &req);

    Message resp;
    recv_message(fd, &resp);

    if (resp.status == RESP_SUCCESS) {
        printf("[SUCCESS] Task %d deleted.\n", id);
    } else {
        printf("[ERROR] Could not delete task (wrong ID?).\n");
    }

    close(fd);
}

// Command: watch (Live Notifications)
void cmd_watch() {
    int fd = connect_to_daemon();
    if (fd == -1) return;

    printf("[INFO] Connected to daemon in WATCH mode. Press Ctrl+C to stop.\n");

    Message req;
    memset(&req, 0, sizeof(req));
    req.type = REQ_SUBSCRIBE;

    if (send_message(fd, &req) == -1) {
        perror("send subscribe");
        close(fd);
        return;
    }

    // Wait for acknowledgment
    Message resp;
    if (recv_message(fd, &resp) != 0 || resp.status != RESP_SUCCESS) {
        printf("[ERROR] Failed to subscribe.\n");
        close(fd);
        return;
    }

    printf("[INFO] Subscribed successfully. Waiting for updates...\n");

    while (recv_message(fd, &resp) == 0) {
        switch (resp.type) {
            case REQ_CREATE:
                printf("[NOTIF] NEW Task: [%d] %s\n", resp.task_data.id, resp.task_data.title);
                break;
            case REQ_UPDATE:
                printf("[NOTIF] UPDATE Task [%d]: Status=%s\n", resp.task_data.id, resp.task_data.status);
                break;
            case REQ_DELETE:
                printf("[NOTIF] DELETE Task [%d]\n", resp.task_id);
                break;
            default:
                printf("[NOTIF] Unknown event type %d\n", resp.type);
                break;
        }
    }

    printf("[INFO] Disconnected from daemon.\n");
    close(fd);
}

// Command: export (to CSV)
void cmd_export(const char *filename) {
    int fd = connect_to_daemon();
    if (fd == -1) return;

    Message req;
    memset(&req, 0, sizeof(req));
    req.type = REQ_LIST;
    send_message(fd, &req);

    Message resp;
    recv_message(fd, &resp); // Get count

    if (resp.status == RESP_SUCCESS) {
        FILE *fp = fopen(filename, "w");
        if (!fp) {
            perror("fopen");
            close(fd);
            return;
        }

        int count = resp.count;
        fprintf(fp, "id,title,description,status,priority\n");

        for (int i = 0; i < count; i++) {
            Message task_msg;
            recv_message(fd, &task_msg);
            fprintf(fp, "%d,%s,%s,%s,%d\n",
                task_msg.task_data.id,
                task_msg.task_data.title,
                task_msg.task_data.description,
                task_msg.task_data.status,
                task_msg.task_data.priority);
        }
        fclose(fp);
        printf("[SUCCESS] Exported %d tasks to %s\n", count, filename);
    }
    close(fd);
}

// Command: import (from CSV)
void cmd_import(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("fopen");
        return;
    }

    char line[1024];
    // Lire la première ligne (header) pour détecter le format
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return;
    }

    // Détection automatique : Si le header commence par "id,", c'est le format export complet
    int is_export_format = (strncmp(line, "id,", 3) == 0);

    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        // Suppression du saut de ligne final pour un parsing propre
        line[strcspn(line, "\n")] = 0;

        char *title = NULL;
        char *desc = NULL;
        char *prio_str = NULL;
        
        // Premier token (soit ID, soit Title)
        char *token = strtok(line, ",");

        if (is_export_format) {
            // Format Export: id,title,description,status,priority
            // Le premier token était l'ID, on l'ignore car la DB en générera un nouveau
            
            title = strtok(NULL, ",");       // Col 2: Title
            desc = strtok(NULL, ",");        // Col 3: Description
            strtok(NULL, ",");               // Col 4: Status (Ignoré, cmd_add force 'todo')
            prio_str = strtok(NULL, ",");    // Col 5: Priority
        } else {
            // Format Simple: title,description,priority
            title = token;                   // Col 1: Title
            desc = strtok(NULL, ",");        // Col 2: Description
            prio_str = strtok(NULL, ",");    // Col 3: Priority
        }

        // Ajout de la tâche si les champs obligatoires sont présents
        if (title && desc) {
            int prio = prio_str ? atoi(prio_str) : 0;
            cmd_add(title, desc, prio);
            count++;
        }
    }
    fclose(fp);
    printf("[SUCCESS] Imported %d tasks from %s\n", count, filename);
}

// Command: update
void cmd_update(int id, char *field, char *value) {
    int fd = connect_to_daemon();
    if (fd == -1) return;

    // Étape 1 : Récupérer la tâche existante (READ)
    Message req;
    memset(&req, 0, sizeof(req));
    req.type = REQ_READ;
    req.task_id = id;

    if (send_message(fd, &req) == -1) {
        perror("send fetch");
        close(fd);
        return;
    }

    Message resp;
    if (recv_message(fd, &resp) == -1) {
        perror("recv fetch");
        close(fd);
        return;
    }

    if (resp.status != RESP_SUCCESS) {
        printf("[ERROR] Task %d not found or server error.\n", id);
        close(fd);
        return;
    }

    // Étape 2 : Modifier le champ demandé localement
    // On travaille directement sur resp.task_data qui contient les infos actuelles
    if (strcmp(field, "title") == 0) {
        strncpy(resp.task_data.title, value, sizeof(resp.task_data.title) - 1);
    } 
    else if (strcmp(field, "description") == 0) {
        strncpy(resp.task_data.description, value, sizeof(resp.task_data.description) - 1);
    } 
    else if (strcmp(field, "status") == 0) {
        strncpy(resp.task_data.status, value, sizeof(resp.task_data.status) - 1);
    } 
    else if (strcmp(field, "priority") == 0) {
        resp.task_data.priority = atoi(value);
    } 
    else {
        printf("[ERROR] Unknown field '%s'. Available fields: title, description, status, priority.\n", field);
        close(fd);
        return;
    }

    // Étape 3 : Renvoyer la tâche modifiée (UPDATE)
    // On réutilise la structure de la réponse précédente pour la nouvelle requête
    req.type = REQ_UPDATE;
    req.task_data = resp.task_data; // Copie de la tâche modifiée

    if (send_message(fd, &req) == -1) {
        perror("send update");
        close(fd);
        return;
    }

    // Attente de la confirmation
    if (recv_message(fd, &resp) == 0 && resp.status == RESP_SUCCESS) {
        printf("[SUCCESS] Task %d updated (%s -> %s).\n", id, field, value);
    } else {
        printf("[ERROR] Update failed.\n");
    }

    close(fd);
}

// Command: interactive (Mode avec fork + sémaphore)
void cmd_interactive() {
    // 1. Création/Ouverture du sémaphore pour protéger stdout
    // Valeur initiale 1 (mutex)
    sem_t *ui_sem = sem_open(SEM_UI_NAME, O_CREAT, 0644, 1);
    if (ui_sem == SEM_FAILED) {
        perror("sem_open");
        return;
    }

    printf("[INFO] Starting Interactive Mode. Type 'exit' to quit.\n");
    printf("Commands: list, add <title> <desc> <prio>, delete <id>, update <id> <field> <val>\n");

    // 2. Fork pour séparer écoute (notifications) et saisie
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // --- ENFANT : Écoute les notifications ---
        int fd = connect_to_daemon();
        if (fd == -1) exit(EXIT_FAILURE);

        Message req, resp;
        memset(&req, 0, sizeof(req));
        req.type = REQ_SUBSCRIBE;
        send_message(fd, &req);
        recv_message(fd, &resp); // Ack inscription

        while (recv_message(fd, &resp) == 0) {
            // Section critique : Affichage de la notification
            sem_wait(ui_sem);
            
            // On efface la ligne courante (prompt) pour afficher la notif proprement
            printf("\r\033[K"); 
            printf("🔔 [NOTIF] Task %d : Action %d\n", resp.task_data.id, resp.type);
            
            // On réaffiche le prompt pour l'utilisateur
            printf("> "); 
            fflush(stdout);
            
            sem_post(ui_sem);
        }
        close(fd);
        exit(0);
    } else {
        // --- PARENT : Saisie utilisateur ---
        char buffer[256];
        while (1) {
            // Affichage du prompt protégé
            sem_wait(ui_sem);
            printf("> ");
            fflush(stdout);
            sem_post(ui_sem);

            // Attente saisie (bloquant, mais le child continue d'afficher grâce au fflush)
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;
            buffer[strcspn(buffer, "\n")] = 0;

            if (strcmp(buffer, "exit") == 0) break;
            if (strlen(buffer) == 0) continue;

            sem_wait(ui_sem);

            // Parsing simple pour appeler les fonctions existantes
            char *cmd = strtok(buffer, " ");
            
            if (strcmp(cmd, "list") == 0) {
                cmd_list();
            } 
            else if (strcmp(cmd, "add") == 0) {
                char *title = strtok(NULL, " ");
                char *desc = strtok(NULL, " ");
                char *prio_str = strtok(NULL, " ");
                if (title && desc && prio_str) {
                    cmd_add(title, desc, atoi(prio_str));
                } else {
                    printf("Usage: add <title> <desc> <prio>\n");
                }
            }
            else if (strcmp(cmd, "delete") == 0) {
                char *id_str = strtok(NULL, " ");
                if (id_str) cmd_delete(atoi(id_str));
            }
            else if (strcmp(cmd, "update") == 0) {
                char *id_str = strtok(NULL, " ");
                char *field = strtok(NULL, " ");
                char *val = strtok(NULL, " ");
                if (id_str && field && val) {
                    // Assurez-vous d'avoir ajouté cmd_update comme vu précédemment
                    // sinon commentez cette ligne
                    cmd_update(atoi(id_str), field, val); 
                }
            }
            else {
                printf("Unknown command.\n");
            }
            
            sem_post(ui_sem);
        }

        // Nettoyage
        kill(pid, SIGTERM);
        wait(NULL);
        sem_close(ui_sem);
        sem_unlink(SEM_UI_NAME);
        printf("Bye.\n");
    }
}

void print_usage(const char *prog_name) {
    printf("Usage:\n");
    printf("  %s list\n", prog_name);
    printf("  %s add <title> [description] [priority]\n", prog_name);
    printf("  %s update <id> <field> <value>\n", prog_name);
    printf("  %s delete <id>\n", prog_name);
    printf("  %s watch\n", prog_name);
    printf("  %s interactive (mode interactif avec notifications live)\n", prog_name);
    printf("  %s export <filename.csv>\n", prog_name);
    printf("  %s import <filename.csv> (Format: title,desc,priority)\n", prog_name);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "list") == 0) {
        cmd_list();
    } 

    else if (strcmp(argv[1], "add") == 0) {
        if (argc < 3) {
            printf("Usage: %s add <title> [description] [priority]\n", argv[0]);
            return EXIT_FAILURE;
        }
        char *desc = (argc >= 4) ? argv[3] : "";
        int prio = (argc >= 5) ? atoi(argv[4]) : 0;
        cmd_add(argv[2], desc, prio);
    } 

    else if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) {
            printf("Usage: %s delete <id>\n", argv[0]);
            return EXIT_FAILURE;
        }
        cmd_delete(atoi(argv[2]));
    }

    else if (strcmp(argv[1], "update") == 0) {
        if (argc < 5) {
            printf("Usage: %s update <id> <field> <value>\n", argv[0]);
            printf("Fields: title, description, status, priority\n");
            return EXIT_FAILURE;
        }
        // argv[2]=ID, argv[3]=Field, argv[4]=Value
        cmd_update(atoi(argv[2]), argv[3], argv[4]);
    }

    else if (strcmp(argv[1], "watch") == 0) {
        cmd_watch();
    }

    else if (strcmp(argv[1], "export") == 0) {
        if (argc < 3) {
            printf("Usage: %s export <file>\n", argv[0]);
            return EXIT_FAILURE;
        }
        cmd_export(argv[2]);
    }

    else if (strcmp(argv[1], "import") == 0) {
        if (argc < 3) {
            printf("Usage: %s import <file>\n", argv[0]);
            return EXIT_FAILURE;
        }
        cmd_import(argv[2]);
    }

    else if (strcmp(argv[1], "interactive") == 0) {
        cmd_interactive();
    }

    else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}