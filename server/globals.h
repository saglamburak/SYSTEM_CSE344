// Dosya Yolu: final/server/globals.h

#ifndef GLOBALS_H
#define GLOBALS_H

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h> // FILE için
#include <signal.h> // sig_atomic_t için
#include "server_structs.h" // client_t, room_t, file_transfer_request_t için

// Global Değişken Bildirimleri (chatserver.c'de tanımlanacak)
extern client_t clients[MAX_CLIENTS];
extern room_t rooms[MAX_ROOMS];
extern file_transfer_request_t file_upload_queue[MAX_UPLOAD_QUEUE];
extern int file_queue_add_idx;
extern int file_queue_remove_idx;

extern int active_client_count;
extern pthread_mutex_t clients_mutex;
extern pthread_mutex_t rooms_mutex;
extern pthread_mutex_t file_queue_mutex;
extern sem_t items_in_queue;
extern sem_t empty_slots_in_queue;

extern volatile sig_atomic_t server_running;
extern FILE *log_file;
extern pthread_t file_worker_threads_ids[NUM_FILE_WORKERS]; // Worker thread ID'leri


#endif // GLOBALS_H
