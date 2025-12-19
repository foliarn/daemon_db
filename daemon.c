#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pthread.h>

#include "database.h"
#include "protocol.h"

// Variables globales
sqlite3 *db = NULL;
int server_socket = -1;
volatile sig_atomic_t running = 1;

// Mutex pour protéger l'accès à la db
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

// management souscription
int subscribers[MAX_CLIENTS];
pthread_mutex_t sub_mutex = PTHREAD_MUTEX_INITIALIZER;

// init list subscriber
void init_subscribers() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        subscribers[i] = -1;
    }
}

// Ajouter un subscriber
void add_subscriber(int fd) {
    pthread_mutex_lock(&sub_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (subscribers[i] == -1) {
            subscribers[i] = fd;
            printf("[INFO] Client %d subscribed to notifications.\n", fd);
            break;
        }
    }
    pthread_mutex_unlock(&sub_mutex);
}

// Enlever un subscriber
void remove_subscriber(int fd) {
    pthread_mutex_lock(&sub_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (subscribers[i] == fd) {
            subscribers[i] = -1;
            break;
        }
    }
    pthread_mutex_unlock(&sub_mutex);
}

// Broadcast un message à tous les subscribers
void broadcast_change(RequestType type, Task *task, int task_id) {
    pthread_mutex_lock(&sub_mutex);
    
    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.type = type;
    msg.task_id = task_id;
    msg.status = RESP_SUCCESS;
    
    if (task != NULL) {
        msg.task_data = *task;
    } else {
        msg.task_data.id = task_id;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (subscribers[i] != -1) {
            send(subscribers[i], &msg, sizeof(Message), MSG_NOSIGNAL);
        }
    }
    
    pthread_mutex_unlock(&sub_mutex);
}

// Signal handler (CTRL+C)
void handle_signal(int sig) {
    (void)sig; 
    printf("\n[INFO] Shutdown initiated...\n");
    running = 0;
}

// Méthode de nettoyage "propre"
void cleanup() {
    if (server_socket != -1) {
        close(server_socket);
        unlink(SOCKET_PATH);
        printf("[INFO] Socket closed and removed.\n");
    }
    if (db != NULL) {
        db_close(db);
        printf("[INFO] Database closed.\n");
    }
    pthread_mutex_destroy(&db_mutex);
    pthread_mutex_destroy(&sub_mutex);
}

// Gérer la logique du client dans un autre thread
void *client_handler(void *arg) {
    int client_fd = *((int *)arg);
    free(arg); 

    pthread_detach(pthread_self());

    Message msg;
    while (recv_message(client_fd, &msg) == 0) {
        printf("[RECV] Request Type: %d from Client %d\n", msg.type, client_fd);

        Message response;
        memset(&response, 0, sizeof(Message));
        response.type = msg.type; 

        pthread_mutex_lock(&db_mutex);

        switch (msg.type) {
            case REQ_CREATE: {
                int id = db_create_task(db, &msg.task_data);
                if (id >= 0) {
                    response.status = RESP_SUCCESS;
                    response.task_id = id;
                    response.task_data.id = id;
                    printf("[INFO] Task created with ID: %d\n", id);
                    
                    broadcast_change(REQ_CREATE, &msg.task_data, id);
                } else {
                    response.status = RESP_ERROR;
                    printf("[ERROR] Failed to create task.\n");
                }
                send_message(client_fd, &response);
                break;
            }

            case REQ_LIST: {
                Task *tasks = NULL;
                int count = 0;
                if (db_list_tasks(db, &tasks, &count) == 0) {
                    response.status = RESP_SUCCESS;
                    response.count = count;
                    send_message(client_fd, &response);

                    for (int i = 0; i < count; i++) {
                        Message task_msg;
                        memset(&task_msg, 0, sizeof(Message));
                        task_msg.type = REQ_LIST;
                        task_msg.task_data = tasks[i];
                        send_message(client_fd, &task_msg);
                    }
                    free(tasks);
                    printf("[INFO] Sent list of %d tasks.\n", count);
                } else {
                    response.status = RESP_ERROR;
                    send_message(client_fd, &response);
                    printf("[ERROR] Failed to list tasks.\n");
                }
                break;
            }

            case REQ_READ: {
                int rc = db_get_task(db, msg.task_id, &response.task_data);
                if (rc == 0) {
                    response.status = RESP_SUCCESS;
                } else if (rc == -2) {
                    response.status = RESP_NOT_FOUND;
                } else {
                    response.status = RESP_ERROR;
                }
                send_message(client_fd, &response);
                break;
            }

            case REQ_UPDATE: {
                int rc = db_update_task(db, msg.task_data.id, 
                                      msg.task_data.title, 
                                      msg.task_data.description, 
                                      msg.task_data.status, 
                                      msg.task_data.priority);
                if (rc == 0) {
                    response.status = RESP_SUCCESS;
                    printf("[INFO] Task %d updated.\n", msg.task_data.id);
                    
                    Task updated_task;
                    db_get_task(db, msg.task_data.id, &updated_task);
                    broadcast_change(REQ_UPDATE, &updated_task, msg.task_data.id);
                } else if (rc == -2) {
                    response.status = RESP_NOT_FOUND;
                } else {
                    response.status = RESP_ERROR;
                }
                send_message(client_fd, &response);
                break;
            }

            case REQ_DELETE: {
                int rc = db_delete_task(db, msg.task_id);
                if (rc == 0) {
                    response.status = RESP_SUCCESS;
                    printf("[INFO] Task %d deleted.\n", msg.task_id);
                    broadcast_change(REQ_DELETE, NULL, msg.task_id);
                } else if (rc == -2) {
                    response.status = RESP_NOT_FOUND;
                    printf("[WARN] Task %d not found for deletion.\n", msg.task_id);
                } else {
                    response.status = RESP_ERROR; 
                }
                send_message(client_fd, &response);
                break;
            }

            case REQ_SUBSCRIBE:
                add_subscriber(client_fd);
                response.status = RESP_SUCCESS;
                send_message(client_fd, &response);
                break;

            case REQ_UNSUBSCRIBE:
                remove_subscriber(client_fd);
                response.status = RESP_SUCCESS;
                send_message(client_fd, &response);
                break;

            default:
                response.status = RESP_INVALID;
                send_message(client_fd, &response);
                printf("[WARN] Unknown request type.\n");
                break;
        }

        pthread_mutex_unlock(&db_mutex);
    }

    // Cleanup quand on se déconnecte
    remove_subscriber(client_fd);
    close(client_fd);
    printf("[INFO] Client %d disconnected.\n", client_fd);
    return NULL;
}

int main() {
    init_subscribers();

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1) {
        perror("sigaction SIGINT");
        return EXIT_FAILURE;
    }
    if (sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction SIGTERM");
        return EXIT_FAILURE;
    }

    printf("[INFO] Initializing database...\n");
    if (db_init("tasks.db", &db) != 0) {
        fprintf(stderr, "DB init failed\n");
        return EXIT_FAILURE;
    }

    if (db_create_table(db) != 0) {
        fprintf(stderr, "Table creation failed\n");
        db_close(db);
        return EXIT_FAILURE;
    }

    printf("[INFO] Creating UNIX socket...\n");
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        cleanup();
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(SOCKET_PATH); 

    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        cleanup();
        return EXIT_FAILURE;
    }

    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("listen");
        cleanup();
        return EXIT_FAILURE;
    }

    printf("[INFO] Daemon ready! Listening on %s\n", SOCKET_PATH);

    while (running) {
        int *client_fd_ptr = malloc(sizeof(int));
        if (client_fd_ptr == NULL) {
            perror("malloc");
            continue;
        }

        *client_fd_ptr = accept(server_socket, NULL, NULL);
        
        if (*client_fd_ptr == -1) {
            free(client_fd_ptr);
            if (errno == EINTR) continue;
            if (running) perror("accept");
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, client_fd_ptr) != 0) {
            perror("pthread_create");
            close(*client_fd_ptr);
            free(client_fd_ptr);
        }
    }

    cleanup();
    return EXIT_SUCCESS;
}