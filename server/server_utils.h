// Dosya Yolu: final/server/server_utils.h

#ifndef SERVER_UTILS_H
#define SERVER_UTILS_H

#include <stdarg.h> // Variadic fonksiyonlar için
#include <sys/types.h> // size_t için (bazı sistemlerde gerekebilir)

// Prototip Bildirimleri
void server_log(const char *level, const char *message_format, ...);
void send_to_client(int client_socket_fd, const char* message_format, ...);
int is_username_valid(const char *username);
int is_room_name_valid(const char *room_name);
int is_file_type_allowed(const char* filename);
int ensure_directory_exists(const char* path);
char* generate_unique_filepath(const char* base_dir, const char* receiver_username, const char* original_filename, char* out_filepath, size_t out_filepath_size);

// chatserver.c'de tanımlanan ve client_handler.c tarafından kullanılan fonksiyonlar
int is_username_taken(const char *username_to_check, int current_client_idx_to_ignore);
int add_client_to_system(int client_idx, const char* username);
void remove_client_from_system(int client_idx);

#endif // SERVER_UTILS_H
