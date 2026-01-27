#ifndef DAEMON_H
#define DAEMON_H

#include <pthread.h>
#include "protocol.h"


/**
 * Initialise le tableau des abonnés à -1 (vide).
 */
void init_subscribers();

/**
 * Ajoute un client (via son descripteur de fichier) à la liste des abonnés.
 * @param fd File descriptor du client
 */
void add_subscriber(int fd);

/**
 * Retire un client de la liste des abonnés.
 * @param fd File descriptor du client
 */
void remove_subscriber(int fd);

/**
 * Diffuse une notification de changement à tous les clients abonnés.
 * @param type Type de changement (REQ_CREATE, REQ_UPDATE, REQ_DELETE)
 * @param task Pointeur vers la tâche concernée (ou NULL si suppression)
 * @param task_id ID de la tâche concernée
 */
void broadcast_change(RequestType type, Task *task, int task_id);

/* --- Gestion du système et des clients --- */

/**
 * Gestionnaire de signal (SIGINT, SIGTERM).
 * Met la variable globale 'running' à 0 pour arrêter la boucle principale.
 */
void handle_signal(int sig);

/**
 * Libère les ressources (fermeture socket, suppression fichier socket, fermeture DB, mutex).
 */
void cleanup();

/**
 * Fonction exécutée par chaque thread dédié à un client.
 * Boucle sur la réception des messages et traite les requêtes.
 * @param arg Pointeur vers l'int contenant le file descriptor du client
 */
void *client_handler(void *arg);

#endif // DAEMON_H