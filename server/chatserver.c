// Dosya Yolu: final/server/chatserver.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <errno.h>
#include <ctype.h> 

#include "server_structs.h" 
#include "globals.h"        
#include "server_utils.h"   
#include "room_manager.h"   
#include "file_transfer.h"  
#include "client_handler.h" 
#include "../common_defs.h" 

// Global Değişken Tanımlamaları
client_t clients[MAX_CLIENTS];
room_t rooms[MAX_ROOMS];
file_transfer_request_t file_upload_queue[MAX_UPLOAD_QUEUE];
int file_queue_add_idx;    
int file_queue_remove_idx; 

int active_client_count; 
pthread_mutex_t clients_mutex; 
pthread_mutex_t rooms_mutex;   
pthread_mutex_t file_queue_mutex; 
sem_t items_in_queue;
sem_t empty_slots_in_queue;

volatile sig_atomic_t server_running; 
FILE *log_file; 
pthread_t file_worker_threads_ids[NUM_FILE_WORKERS];

// Fonksiyon Prototipi (aynı dosyada tanımlı olsa da iyi bir pratiktir)
void broadcast_server_message(const char *message, int broadcast_to_all, int socket_to_exclude_if_not_all);
int is_username_taken(const char *username_to_check, int current_client_idx_to_ignore);
int add_client_to_system(int client_idx, const char* username);
void remove_client_from_system(int client_idx);


void initialize_server_resources() {
    server_running = 1;
    active_client_count = 0;
    file_queue_add_idx = 0;
    file_queue_remove_idx = 0;

    if (pthread_mutex_init(&clients_mutex, NULL) != 0 ||
        pthread_mutex_init(&rooms_mutex, NULL) != 0 ||
        pthread_mutex_init(&file_queue_mutex, NULL) != 0) {
        perror("FATAL: Ana mutex'lerden biri başlatılamadı");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        clients[i].active = 0;
        clients[i].current_room_idx = -1;
        clients[i].socket_fd = -1; 
    }
    for (int i = 0; i < MAX_ROOMS; ++i) {
        rooms[i].active = 0;
        rooms[i].user_count = 0;
        if (pthread_mutex_init(&rooms[i].room_mutex, NULL) != 0) {
            fprintf(stderr, "FATAL: Oda %d için mutex başlatılamadı: %s\n", i, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < MAX_UPLOAD_QUEUE; ++i) {
        file_upload_queue[i].active_in_queue = 0;
    }

    if (sem_init(&items_in_queue, 0, 0) != 0) { 
        perror("FATAL: sem_init items_in_queue failed");
        exit(EXIT_FAILURE);
    }
    if (sem_init(&empty_slots_in_queue, 0, MAX_UPLOAD_QUEUE) != 0) { 
        perror("FATAL: sem_init empty_slots_in_queue failed");
        sem_destroy(&items_in_queue); 
        exit(EXIT_FAILURE);
    }

    if (!ensure_directory_exists(SERVER_FILES_DIR)) {
        fprintf(stderr, "UYARI: Ana dosya depolama klasörü '%s' oluşturulamadı veya zaten var ama klasör değil.\n", SERVER_FILES_DIR);
    }

    log_file = fopen("server_log.txt", "a"); 
    if (!log_file) {
        perror("UYARI: Log dosyası açılamadı. Loglar sadece konsola yazılacak.");
    }
    server_log("INFO", "Sunucu kaynakları ve semaforlar başlatıldı.");

    for (long i = 0; i < NUM_FILE_WORKERS; ++i) { 
        if (pthread_create(&file_worker_threads_ids[i], NULL, file_transfer_worker, (void*)i) != 0) {
            server_log("FATAL", "Dosya transfer worker thread %ld oluşturulamadı: %s", i, strerror(errno));
            exit(EXIT_FAILURE);
        }
        server_log("INFO", "Dosya transfer worker thread %ld başlatıldı (joinable).", i);
    }
}

void cleanup_server_resources() {
    server_log("INFO", "Sunucu kaynakları temizleniyor.");
    
    for(int i=0; i < NUM_FILE_WORKERS; ++i) { 
        if(sem_post(&items_in_queue) != 0 && errno != EINVAL) { 
             server_log("WARN", "cleanup: items_in_queue post hatası (worker %d): %s", i, strerror(errno));
        }
    }
    for (int i = 0; i < NUM_FILE_WORKERS; ++i) {
        if (file_worker_threads_ids[i] != 0) { 
            server_log("DEBUG", "Worker thread %d için join bekleniyor (cleanup)...", i);
            if (pthread_join(file_worker_threads_ids[i], NULL) != 0 ) {
                if (errno != ESRCH && errno != EINVAL) { 
                     server_log("WARN", "Worker thread %d join edilemedi (cleanup): %s", i, strerror(errno));
                }
            } else {
                 server_log("DEBUG", "Worker thread %d başarıyla join edildi (cleanup).", i);
            }
        }
    }
    server_log("INFO", "Tüm worker thread'ler sonlandı (cleanup).");

    for (int i = 0; i < MAX_ROOMS; ++i) {
         pthread_mutex_destroy(&rooms[i].room_mutex);
    }
    sem_destroy(&items_in_queue);
    sem_destroy(&empty_slots_in_queue);
    pthread_mutex_destroy(&file_queue_mutex); 
    pthread_mutex_destroy(&clients_mutex);
    pthread_mutex_destroy(&rooms_mutex);

    if (log_file) {
        fflush(log_file); 
        fclose(log_file);
        log_file = NULL; 
    }
}

int is_username_taken(const char *username_to_check, int current_client_idx_to_ignore) {
    // Bu fonksiyon çağrılırken clients_mutex kilitli olmalı!
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (i == current_client_idx_to_ignore) continue;
        if (clients[i].active && strcmp(clients[i].username, username_to_check) == 0) {
            return 1;
        }
    }
    return 0;
}

int add_client_to_system(int client_idx, const char* username) {
    // Bu fonksiyon çağrılırken clients_mutex kilitli olmalı!
    if(client_idx < 0 || client_idx >= MAX_CLIENTS || clients[client_idx].socket_fd < 0) {
        server_log("ERROR", "add_client_to_system: Geçersiz client_idx (%d) veya soket.", client_idx);
        return 0; 
    }
    
    strncpy(clients[client_idx].username, username, USERNAME_MAX_LEN);
    clients[client_idx].username[USERNAME_MAX_LEN] = '\0';
    clients[client_idx].active = 1; 
    clients[client_idx].current_room_idx = -1; 
    active_client_count++; // Sadece başarılı giriş yapan aktif kullanıcı sayısı
    return 1;
}

void remove_client_from_system(int client_idx) {
    if(client_idx < 0 || client_idx >= MAX_CLIENTS) return;

    char username_copy[MAX_USERNAME_LEN_INC_NULL] = "N/A";
    int room_idx_copy = -1;
    int socket_fd_copy = -1; 
    int client_was_active = 0;
    
    pthread_mutex_lock(&clients_mutex);
    client_was_active = clients[client_idx].active;
    if (clients[client_idx].socket_fd != -1) { // Soket hala geçerliyse (kullanıcı adı almadan düşmüş olabilir)
        if (client_was_active) {
            strncpy(username_copy, clients[client_idx].username, MAX_USERNAME_LEN_INC_NULL);
            username_copy[MAX_USERNAME_LEN_INC_NULL-1] = '\0';
            active_client_count--; // Sadece aktif kullanıcıysa sayacı azalt
        }
        room_idx_copy = clients[client_idx].current_room_idx;
        socket_fd_copy = clients[client_idx].socket_fd;

        clients[client_idx].active = 0;       
        clients[client_idx].current_room_idx = -1; 
        clients[client_idx].socket_fd = -1; 
        memset(clients[client_idx].username, 0, MAX_USERNAME_LEN_INC_NULL); 
    }
    pthread_mutex_unlock(&clients_mutex); 

    if (socket_fd_copy != -1) { // Eğer bir soket gerçekten kapatılacaksa
        if (client_was_active) {
            if (room_idx_copy != -1) {
                remove_user_from_room(client_idx, room_idx_copy); 
            }
            server_log("DISCONNECT", "user '%s' lost connection. Cleaned up resources.", username_copy);
        } else {
            // Kullanıcı adı almadan bağlantı koptuysa (username_copy "N/A" olabilir)
            server_log("DISCONNECT", "Client (Slot %d, IP: %s) (not logged in) lost connection. Cleaned up resources.", client_idx, clients[client_idx].ip_addr /* Bu bilgi hala duruyor olabilir */);
        }
        
        shutdown(socket_fd_copy, SHUT_RDWR); 
        close(socket_fd_copy);
        fprintf(stderr, "[DEBUG] remove_client_from_system: Client '%s' (Slot: %d) kaynakları temizlendi, soket kapatıldı.\n", username_copy, client_idx);
    } else {
        fprintf(stderr, "[DEBUG] remove_client_from_system: Client (Slot: %d) için zaten kapatılmış/temizlenmiş soket.\n", client_idx);
    }
}


void broadcast_server_message(const char *message, int broadcast_to_all, int socket_to_exclude_if_not_all) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].socket_fd != -1) {
            if (broadcast_to_all || clients[i].socket_fd != socket_to_exclude_if_not_all) {
                 send_to_client(clients[i].socket_fd, "%s", message);
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void sigint_handler(int signum) {
    (void)signum; 
    if (!server_running) { 
        return; 
    }
    char msg_sigint[] = "\nSIGINT alındı. Sunucu kapatılıyor (async-safe msg)...\n";
    write(STDOUT_FILENO, msg_sigint, strlen(msg_sigint)); 
    
    server_running = 0; 

    for(int i=0; i < NUM_FILE_WORKERS + MAX_UPLOAD_QUEUE ; ++i) { 
        if(sem_post(&items_in_queue) != 0 && errno != EINVAL) { }
    }
}


int main(int argc, char *argv[]) {
    int server_socket_fd = -1; 
    int client_socket_fd_temp;
    struct sockaddr_in server_addr, client_addr_temp;
    socklen_t client_addr_len = sizeof(client_addr_temp);

    if (argc != 2) {
        fprintf(stderr, "Kullanım: ./chatserver <port>\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    if (port < 1024 || port > 65535) { 
        fprintf(stderr, "Geçersiz port numarası: %s. (1024-65535 arası olmalıdır)\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("FATAL: sigaction SIGINT error");
        exit(EXIT_FAILURE);
    }

    initialize_server_resources(); 

    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd == -1) {
        server_log("FATAL", "Soket oluşturulamadı: %s", strerror(errno));
        cleanup_server_resources();
        exit(EXIT_FAILURE);
    }
    server_log("INFO", "Sunucu soketi (FD: %d) oluşturuldu.", server_socket_fd);

    int opt = 1;
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        server_log("FATAL", "setsockopt(SO_REUSEADDR) başarısız oldu: %s", strerror(errno));
        if(server_socket_fd != -1) close(server_socket_fd);
        cleanup_server_resources();
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        server_log("FATAL", "Soket %d portuna bağlanamadı: %s", port, strerror(errno));
        if(server_socket_fd != -1) close(server_socket_fd);
        cleanup_server_resources();
        exit(EXIT_FAILURE);
    }
    server_log("INFO", "Soket %d portuna bağlandı.", port);

    if (listen(server_socket_fd, MAX_PENDING_CONNECTIONS) < 0) { 
        server_log("FATAL", "Dinleme başarısız oldu: %s", strerror(errno));
        if(server_socket_fd != -1) close(server_socket_fd);
        cleanup_server_resources();
        exit(EXIT_FAILURE);
    }
    server_log("INFO", "Sunucu %d portunda dinlemede...", port);

    while (server_running) {
        client_socket_fd_temp = accept(server_socket_fd, (struct sockaddr *)&client_addr_temp, &client_addr_len);
        
        if (!server_running) {
            if (client_socket_fd_temp >= 0) {
                send_to_client(client_socket_fd_temp, KYEL "[Server]: Sunucu kapanmakta olduğu için yeni bağlantı kabul edilmiyor.\n" KNRM);
                close(client_socket_fd_temp);
            }
            break;
        }

        if (client_socket_fd_temp < 0) {
            if (errno == EINTR && !server_running) { 
                 server_log("DEBUG", "accept() SIGINT ile kesildi, sunucu kapanıyor.");
                 break; 
            } else if (server_running) { 
                server_log("ERROR", "Bağlantı kabul edilemedi: %s", strerror(errno));
            }
            continue; 
        }

        // Yeni bağlantı için boş bir slot ara. clients_mutex ile korunmalı.
        pthread_mutex_lock(&clients_mutex);
        int client_idx = -1;
        for(int i = 0; i < MAX_CLIENTS; ++i) {
            // Bir slotun boş olması için hem active=0 hem de socket_fd=-1 olmalı.
            // socket_fd=-1, o slotun bir thread tarafından kullanılmadığını veya
            // önceki bir bağlantının düzgünce temizlendiğini gösterir.
            if (!clients[i].active && clients[i].socket_fd == -1) { 
                client_idx = i;
                clients[i].socket_fd = client_socket_fd_temp; // Slotu bu yeni bağlantı için ayır
                inet_ntop(AF_INET, &client_addr_temp.sin_addr, clients[i].ip_addr, INET_ADDRSTRLEN);
                clients[i].port = ntohs(client_addr_temp.sin_port);
                // clients[i].active hala 0, kullanıcı adı alınana kadar
                clients[i].current_room_idx = -1;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        
        if (client_idx == -1) { // Tüm MAX_CLIENTS slotları şu anda bir bağlantı tarafından tutuluyor (aktif olmasalar bile)
            server_log("WARN", "Maksimum bağlantı slotu (%d) dolu. Yeni bağlantı reddedildi.", MAX_CLIENTS);
            send_to_client(client_socket_fd_temp, KRED "[Server]: Sunucu maksimum bağlantı kapasitesinde. Lütfen daha sonra tekrar deneyin.\n" KNRM);
            close(client_socket_fd_temp);
            continue;
        }

        server_log("DEBUG", "Geçici bağlantı kabul edildi: %s:%d (Slot: %d, FD: %d)", 
                   clients[client_idx].ip_addr, clients[client_idx].port, client_idx, client_socket_fd_temp);

        int *p_client_idx = malloc(sizeof(int));
        if (!p_client_idx) {
            server_log("ERROR", "Thread argümanı için bellek ayrılamadı (malloc failed)");
            close(client_socket_fd_temp); 
            pthread_mutex_lock(&clients_mutex); 
            clients[client_idx].socket_fd = -1; // Ayrılan slotu serbest bırak
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }
        *p_client_idx = client_idx;
        
        pthread_t new_client_thread_id;
        if (pthread_create(&new_client_thread_id, NULL, handle_client, (void *)p_client_idx) != 0) {
            server_log("ERROR", "İstemci thread'i oluşturulamadı: %s", strerror(errno));
            free(p_client_idx);
            close(client_socket_fd_temp);
            pthread_mutex_lock(&clients_mutex);
            clients[client_idx].socket_fd = -1; // Ayrılan slotu serbest bırak
            pthread_mutex_unlock(&clients_mutex);
        } else {
            pthread_mutex_lock(&clients_mutex);
            clients[client_idx].thread_id = new_client_thread_id; 
            pthread_mutex_unlock(&clients_mutex);
            pthread_detach(new_client_thread_id); 
        }
    } 

    server_log("INFO", "Sunucu ana bağlantı kabul döngüsü sonlandı.");
    
    if (!server_running) { // Eğer SIGINT ile çıkıldıysa
        server_log("SHUTDOWN", "Sunucu kapanıyor. Tüm aktif istemcilere bildirim gönderiliyor...");
        broadcast_server_message(KYEL "\n[Server]: Sunucu kapatılıyor. Bağlantınız sonlandırılacak.\n" KNRM, 1, -1);
        sleep(1); // Mesajların gitmesi için kısa bir bekleme
    }

    if(server_socket_fd != -1) {
        server_log("INFO", "Sunucu dinleme soketi (FD: %d) kapatılıyor.", server_socket_fd);
        if(shutdown(server_socket_fd, SHUT_RDWR) != 0 && errno != ENOTCONN){
             server_log("WARN", "Dinleme soketi shutdown edilemedi: %s", strerror(errno));
        }
        close(server_socket_fd);
        server_socket_fd = -1;
    }
    
    int final_active_clients_at_shutdown = 0; 
    pthread_mutex_lock(&clients_mutex);
    final_active_clients_at_shutdown = active_client_count; 
    pthread_mutex_unlock(&clients_mutex);
    server_log("SHUTDOWN", "Disconnecting %d clients, saving logs.", final_active_clients_at_shutdown);


    cleanup_server_resources(); 
    printf("Sunucu başarıyla kapatıldı (log dosyası kapatıldı).\n"); 
    return 0;
}
