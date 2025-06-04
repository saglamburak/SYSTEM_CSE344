// Dosya Yolu: final/server/room_manager.h

#ifndef ROOM_MANAGER_H
#define ROOM_MANAGER_H

#include "server_structs.h" // room_t tanımı için

// Prototip Bildirimleri
int find_or_create_room(const char *room_name);
void add_user_to_room(int client_idx, int room_idx);
void remove_user_from_room(int client_idx, int room_idx_to_leave);
void broadcast_message_to_room(int room_idx, const char *message, int sender_client_idx, const char* sender_username_override);

#endif // ROOM_MANAGER_H