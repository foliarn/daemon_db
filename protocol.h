#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "database.h"  // Pour la struct Task

// CONSTANTES

#define SOCKET_PATH "/tmp/taskd.sock"  // Chemin du socket UNIX
#define MAX_CLIENTS 32                 // Nombre max de clients simultanés

// TYPES DE REQUÊTES 

typedef enum {
    REQ_CREATE,      // Créer une tâche
    REQ_READ,        // Lire une tâche par ID
    REQ_LIST,        // Lister toutes les tâches
    REQ_UPDATE,      // Modifier une tâche
    REQ_DELETE,      // Supprimer une tâche
    REQ_SUBSCRIBE,   // S'abonner aux notifications
    REQ_UNSUBSCRIBE  // Se désabonner
} RequestType;

// CODES DE RÉPONSE

typedef enum {
    RESP_SUCCESS = 0,    // Opération réussie
    RESP_ERROR = -1,     // Erreur générale
    RESP_NOT_FOUND = -2, // Tâche introuvable
    RESP_INVALID = -3    // Requête invalide
} ResponseCode;

// STRUCTURE MESSAGE

typedef struct {
    RequestType type;       // Type de requête
    int task_id;           // ID de la tâche (si applicable)
    Task task_data;        // Données de la tâche
    ResponseCode status;   // Code de retour (pour les réponses)
    int count;             // Nombre de tâches (pour LIST)
} Message;

//FONCTIONS UTILITAIRES 

// Envoyer un message complet
// Retourne 0 si succès, -1 si erreur
static inline int send_message(int socket_fd, const Message *msg) {
    ssize_t sent = send(socket_fd, msg, sizeof(Message), 0);
    return (sent == sizeof(Message)) ? 0 : -1;
}

// Recevoir un message complet
// Retourne 0 si succès, -1 si erreur
static inline int recv_message(int socket_fd, Message *msg) {
    ssize_t received = recv(socket_fd, msg, sizeof(Message), 0);
    return (received == sizeof(Message)) ? 0 : -1;
} 

#endif