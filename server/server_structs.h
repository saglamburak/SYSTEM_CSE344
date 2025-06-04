// Dosya Yolu: final/server/server_structs.h

#ifndef SERVER_STRUCTS_H
#define SERVER_STRUCTS_H

#include <pthread.h>
#include <arpa/inet.h> 
#include <time.h> 
#include "../common_defs.h" 

// PDF "Supports at least 15 concurrent clients" diyor.
// Kullanıcının isteği üzerine, kullanıcı adı sorma aşamasında 32 client bekleyebilsin.
// Bu, toplam bağlantı slot sayısının 32 olması gerektiği anlamına gelir.
#define MAX_CLIENTS 32      // Toplam yönetilebilecek bağlantı slotu sayısı
#define MAX_PENDING_CONNECTIONS 35 // listen() için backlog, MAX_CLIENTS'ten biraz fazla
#define MAX_ROOMS 10
#define MAX_USERS_PER_ROOM 15 
#define MAX_FILE_SIZE (3 * 1024 * 1024) 
#define MAX_UPLOAD_QUEUE 5             
#define NUM_FILE_WORKERS 2 
#define SERVER_FILES_DIR "server_files"

typedef struct {
    char name[MAX_ROOM_NAME_LEN_INC_NULL];
    int users[MAX_USERS_PER_ROOM]; 
    int user_count;
    int active; 
    pthread_mutex_t room_mutex; 
} room_t;

typedef struct {
    int socket_fd;
    char username[MAX_USERNAME_LEN_INC_NULL];
    char ip_addr[INET_ADDRSTRLEN];
    int port;
    int active; // Kullanıcı adı alınıp sisteme eklendikten sonra 1 olur        
    int current_room_idx; 
    pthread_t thread_id; 
} client_t;

typedef struct {
    char sender_username[MAX_USERNAME_LEN_INC_NULL];
    int sender_client_idx; 
    char receiver_username[MAX_USERNAME_LEN_INC_NULL];
    char filename[256]; 
    long filesize;
    int active_in_queue; 
    time_t enqueued_time; 
} file_transfer_request_t;

#endif // SERVER_STRUCTS_H
