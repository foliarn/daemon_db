#ifndef CLIENT_H
#define CLIENT_H

/**
 * Établit une connexion au socket UNIX du daemon.
 * Retourne le descripteur de fichier (fd) ou -1 en cas d'erreur.
 */
int connect_to_daemon();

/**
 * Envoie une requête REQ_LIST au daemon et affiche les tâches reçues.
 */
void cmd_list();

/**
 * Envoie une requête REQ_CREATE au daemon.
 * @param title Titre de la tâche
 * @param description Description de la tâche
 * @param priority Priorité (entier)
 */
void cmd_add(char *title, char *description, int priority);

/**
 * Envoie une requête REQ_DELETE au daemon.
 * @param id ID de la tâche à supprimer
 */
void cmd_delete(int id);

/**
 * Envoie une requête REQ_UPDATE au daemon.
 * @param id ID de la tâche à modifier
 * @param field Nom du champ à modifier ("title", "description", "status", "priority")
 * @param value Nouvelle valeur sous forme de chaîne
 */
void cmd_update(int id, char *field, char *value);

/**
 * Mode surveillance : s'abonne aux notifications et les affiche en temps réel.
 * Bloquant jusqu'à interruption (Ctrl+C).
 */
void cmd_watch();

/**
 * Exporte la liste des tâches actuelle dans un fichier CSV.
 * @param filename Chemin du fichier de sortie
 */
void cmd_export(const char *filename);

/**
 * Importe des tâches depuis un fichier CSV.
 * @param filename Chemin du fichier d'entrée
 */
void cmd_import(const char *filename);

/**
 * Mode interactif avancé avec invite de commande et notifications asynchrones.
 * Utilise fork() et des sémaphores.
 */
void cmd_interactive();

/**
 * Affiche l'aide et l'utilisation du programme.
 * @param prog_name Nom de l'exécutable (argv[0])
 */
void print_usage(const char *prog_name);

#endif // CLIENT_H