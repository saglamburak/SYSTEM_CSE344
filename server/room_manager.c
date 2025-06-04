// Dosya Yolu: final/server/room_manager.c

#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "room_manager.h"
#include "globals.h"      
#include "server_utils.h" 
#include "../common_defs.h" 

int find_or_create_room(const char *room_name) {
    // Bu fonksiyon çağrılırken rooms_mutex kilitli olmalı!
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (rooms[i].active && strcmp(rooms[i].name, room_name) == 0) {
            return i; 
        }
    }
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (!rooms[i].active) {
            strncpy(rooms[i].name, room_name, MAX_ROOM_NAME_LEN_INC_NULL -1); 
            rooms[i].name[MAX_ROOM_NAME_LEN_INC_NULL-1] = '\0'; 
            rooms[i].active = 1;
            rooms[i].user_count = 0;
            server_log("INFO", "Yeni oda oluşturuldu: '%s' (Room Idx: %d)", rooms[i].name, i);
            return i;
        }
    }
    return -1; 
}

void add_user_to_room(int client_idx, int room_idx) {
    if (room_idx < 0 || room_idx >= MAX_ROOMS) {
        server_log("ERROR", "add_user_to_room: Geçersiz oda index'i: %d", room_idx);
        return;
    }
    if (client_idx < 0 || client_idx >= MAX_CLIENTS) {
        server_log("ERROR", "add_user_to_room: Geçersiz client index'i: %d", client_idx);
        return;
    }

    char username_copy[MAX_USERNAME_LEN_INC_NULL];
    int client_socket_fd_copy = -1;
    int client_is_globally_active = 0;

    pthread_mutex_lock(&clients_mutex);
    client_is_globally_active = clients[client_idx].active;
    if (client_is_globally_active) {
        strncpy(username_copy, clients[client_idx].username, MAX_USERNAME_LEN_INC_NULL-1);
        username_copy[MAX_USERNAME_LEN_INC_NULL-1] = '\0';
        client_socket_fd_copy = clients[client_idx].socket_fd;
    }
    pthread_mutex_unlock(&clients_mutex);

    if (!client_is_globally_active) { 
        server_log("WARN", "add_user_to_room: Pasif istemci (idx: %d) odaya eklenemez.", client_idx);
        return;
    }

    pthread_mutex_lock(&rooms[room_idx].room_mutex); // Oda için kilidi al
    if (!rooms[room_idx].active) { 
        pthread_mutex_unlock(&rooms[room_idx].room_mutex);
        if (client_socket_fd_copy != -1) send_to_client(client_socket_fd_copy, KRED "[Server]: Odaya katılım başarısız ('%s' artık aktif değil).\n" KNRM, rooms[room_idx].name);
        return;
    }

    for(int i=0; i < rooms[room_idx].user_count; ++i) {
        if(rooms[room_idx].users[i] == client_idx) {
            pthread_mutex_unlock(&rooms[room_idx].room_mutex);
            if (client_socket_fd_copy != -1) send_to_client(client_socket_fd_copy, KYEL "[Server]: Zaten '%s' odasındasınız.\n" KNRM, rooms[room_idx].name);
            return;
        }
    }

    if (rooms[room_idx].user_count < MAX_USERS_PER_ROOM) {
        rooms[room_idx].users[rooms[room_idx].user_count++] = client_idx;
        
        pthread_mutex_lock(&clients_mutex);
        clients[client_idx].current_room_idx = room_idx; 
        pthread_mutex_unlock(&clients_mutex);
        
        server_log("JOIN", "Kullanıcı '%s' (Slot: %d) '%s' odasına katıldı. Odadaki kullanıcı sayısı: %d",
                username_copy, client_idx, rooms[room_idx].name, rooms[room_idx].user_count);
        
        if (client_socket_fd_copy != -1) send_to_client(client_socket_fd_copy, KGRN "[Server]: '%s' odasına katıldınız.\n" KNRM, rooms[room_idx].name);

        char announcement[BUFFER_SIZE];
        snprintf(announcement, BUFFER_SIZE, KYEL "[%s] odaya katıldı." KNRM, username_copy);
        // broadcast_message_to_room çağrılırken rooms[room_idx].room_mutex zaten kilitli.
        broadcast_message_to_room(room_idx, announcement, client_idx, NULL); 
    } else {
        if(client_socket_fd_copy != -1) send_to_client(client_socket_fd_copy, KRED "[Server]: '%s' odası dolu (Maks %d kullanıcı).\n" KNRM, rooms[room_idx].name, MAX_USERS_PER_ROOM);
        server_log("WARN", "Kullanıcı '%s' dolu olan '%s' odasına katılamadı.", username_copy, rooms[room_idx].name);
    }
    pthread_mutex_unlock(&rooms[room_idx].room_mutex); // Oda kilidini aç
}

// remove_user_from_room artık hem client_idx hem de room_idx_to_leave alıyor.
// Bu fonksiyon, client'ın global listesindeki current_room_idx'i DEĞİŞTİRMEZ.
// O sorumluluk çağıran fonksiyondadır (clients_mutex altında).
void remove_user_from_room(int client_idx, int room_idx_to_leave) {
    if (room_idx_to_leave < 0 || room_idx_to_leave >= MAX_ROOMS) {
        return; 
    }

    char username_for_log[MAX_USERNAME_LEN_INC_NULL]; 
    
    pthread_mutex_lock(&clients_mutex); 
    if (clients[client_idx].socket_fd != -1 && strlen(clients[client_idx].username) > 0) { 
        strncpy(username_for_log, clients[client_idx].username, MAX_USERNAME_LEN_INC_NULL -1);
    } else {
        strncpy(username_for_log, "(Bilinmiyor)", MAX_USERNAME_LEN_INC_NULL -1); 
    }
    username_for_log[MAX_USERNAME_LEN_INC_NULL-1] = '\0';
    pthread_mutex_unlock(&clients_mutex);


    pthread_mutex_lock(&rooms[room_idx_to_leave].room_mutex); // Oda için kilidi al
    if (!rooms[room_idx_to_leave].active) { 
        pthread_mutex_unlock(&rooms[room_idx_to_leave].room_mutex);
        return;
    }

    int found_in_list = 0;
    for (int i = 0; i < rooms[room_idx_to_leave].user_count; ++i) {
        if (rooms[room_idx_to_leave].users[i] == client_idx) {
            rooms[room_idx_to_leave].users[i] = rooms[room_idx_to_leave].users[rooms[room_idx_to_leave].user_count - 1];
            rooms[room_idx_to_leave].user_count--;
            found_in_list = 1;
            break;
        }
    }

    if (found_in_list) {
        server_log("ROOM", "Kullanıcı '%s' (Slot: %d) '%s' odasından (listeden) çıkarıldı. Odadaki kullanıcı sayısı: %d",
                username_for_log, client_idx, rooms[room_idx_to_leave].name, rooms[room_idx_to_leave].user_count);

        char announcement[BUFFER_SIZE];
        snprintf(announcement, BUFFER_SIZE, KYEL "[%s] odadan ayrıldı." KNRM, username_for_log);
        // broadcast_message_to_room çağrılırken rooms[room_idx_to_leave].room_mutex zaten kilitli.
        broadcast_message_to_room(room_idx_to_leave, announcement, -1, NULL); 
    }
    pthread_mutex_unlock(&rooms[room_idx_to_leave].room_mutex); // Oda kilidini aç
}

// YENİ MANTIK: Bu fonksiyon çağrılırken rooms[room_idx].room_mutex'in ÇAĞIRAN TARAFINDAN
// kilitlenmiş olduğu varsayılır. Bu fonksiyon kendi içinde kilitleme/açma yapmaz.
void broadcast_message_to_room(int room_idx, const char *message, int sender_client_idx, const char* sender_username_override) {
    if (room_idx < 0 || room_idx >= MAX_ROOMS || !rooms[room_idx].active) {
        // server_log("DEBUG", "broadcast_message_to_room: Geçersiz oda (%d) veya pasif.", room_idx);
        return;
    }

    char formatted_message_body[BUFFER_SIZE];
    strncpy(formatted_message_body, message, sizeof(formatted_message_body)-1);
    formatted_message_body[sizeof(formatted_message_body)-1] = '\0';
    if (strlen(formatted_message_body) > 0 && formatted_message_body[strlen(formatted_message_body)-1] != '\n') {
        strncat(formatted_message_body, "\n", sizeof(formatted_message_body) - strlen(formatted_message_body) - 1);
    }

    char full_message_to_send[BUFFER_SIZE + MAX_USERNAME_LEN_INC_NULL + 20]; 
    
    if (sender_username_override && sender_username_override[0] != '\0') { 
         snprintf(full_message_to_send, sizeof(full_message_to_send), KBLU "[%s]: %s" KNRM, sender_username_override, formatted_message_body);
    } else { 
         strncpy(full_message_to_send, formatted_message_body, sizeof(full_message_to_send)-1);
         full_message_to_send[sizeof(full_message_to_send)-1] = '\0';
    }
    
    // Oda kullanıcı listesini okurken rooms[room_idx].room_mutex zaten kilitli olmalı.
    for (int i = 0; i < rooms[room_idx].user_count; ++i) {
        int current_client_idx_in_room = rooms[room_idx].users[i];
        
        if (sender_username_override != NULL && current_client_idx_in_room == sender_client_idx) {
            continue; // Kendine broadcast yapma (eğer kullanıcı mesajıysa)
        }

        // Alıcının soketini almak için clients_mutex'i kilitle
        pthread_mutex_lock(&clients_mutex);
        int receiver_socket_fd = -1;
        if (clients[current_client_idx_in_room].active && clients[current_client_idx_in_room].socket_fd != -1) { 
            receiver_socket_fd = clients[current_client_idx_in_room].socket_fd;
        }
        pthread_mutex_unlock(&clients_mutex);

        if (receiver_socket_fd != -1) {
            send_to_client(receiver_socket_fd, "%s", full_message_to_send);
        }
    }
}
