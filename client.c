#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "protocol.h"

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
    // Skip header
    fgets(line, sizeof(line), fp);

    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        // Simple CSV parser (assumes no commas in fields for simplicity)
        char *title = strtok(line, ",");
        char *desc = strtok(NULL, ",");
        char *prio_str = strtok(NULL, ","); // Skipping status/id for import logic, or adapting

        // NOTE: Our simple import expects: title,description,priority
        // If importing from our own export, the format is id,title,description,status,priority
        // Let's adapt to read title(2), description(3), priority(5) roughly
        // Ideally, user should provide a file with "title,desc,prio"
        
        // For robustness in this demo, let's just assume the user provides Title,Desc,Priority
        if (title && desc) {
            int prio = prio_str ? atoi(prio_str) : 0;
            // Remove newlines
            title[strcspn(title, "\n")] = 0;
            desc[strcspn(desc, "\n")] = 0;
            
            cmd_add(title, desc, prio);
            count++;
        }
    }
    fclose(fp);
    printf("[SUCCESS] Imported %d tasks from %s\n", count, filename);
}

void print_usage(const char *prog_name) {
    printf("Usage:\n");
    printf("  %s list\n", prog_name);
    printf("  %s add <title> [description] [priority]\n", prog_name);
    printf("  %s delete <id>\n", prog_name);
    printf("  %s watch\n", prog_name);
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
    else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}