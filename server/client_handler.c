// Dosya Yolu: final/server/client_handler.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h> 

#include "client_handler.h"
#include "globals.h"      
#include "server_utils.h" 
#include "room_manager.h" 
#include "file_transfer.h"
#include "../common_defs.h" 

void *handle_client(void *client_idx_ptr) {
    int client_idx = *((int *)client_idx_ptr);
    free(client_idx_ptr);
    
    int client_socket_fd = -1;
    char client_ip_copy[INET_ADDRSTRLEN] = "N/A";
    char current_username_safe[MAX_USERNAME_LEN_INC_NULL] = "N/A";

    pthread_mutex_lock(&clients_mutex);
    if (client_idx >= 0 && client_idx < MAX_CLIENTS && clients[client_idx].socket_fd != -1) {
        client_socket_fd = clients[client_idx].socket_fd;
        strncpy(client_ip_copy, clients[client_idx].ip_addr, INET_ADDRSTRLEN-1);
        client_ip_copy[INET_ADDRSTRLEN-1] = '\0';
    } else {
        pthread_mutex_unlock(&clients_mutex);
        server_log("ERROR", "handle_client (Slot %d) için geçersiz başlangıç durumu veya soket zaten kapatılmış.", client_idx);
        return NULL;
    }
    pthread_mutex_unlock(&clients_mutex);

    char buffer[BUFFER_SIZE];
    char username_buffer[MAX_USERNAME_LEN_INC_NULL];
    ssize_t bytes_received;
    int username_accepted = 0;

    // İlk kullanıcı adı isteme mesajı bir kez gönderilir.
    send_to_client(client_socket_fd, "[Server]: Kullanıcı adınızı girin (maks %d karakter, alfanumerik):\n", USERNAME_MAX_LEN);

    // Kullanıcı adı geçerli olana kadar döngü
    while (server_running && !username_accepted) {
        bytes_received = recv(client_socket_fd, username_buffer, MAX_USERNAME_LEN_INC_NULL - 1, 0);

        if (bytes_received <= 0) {
            server_log("WARN", "Kullanıcı adı alma sırasında bağlantı kesildi/hata (FD: %d, IP: %s, Slot: %d)", client_socket_fd, client_ip_copy, client_idx);
            if(client_socket_fd != -1) close(client_socket_fd); 
            pthread_mutex_lock(&clients_mutex);
            clients[client_idx].socket_fd = -1; 
            clients[client_idx].active = 0; 
            pthread_mutex_unlock(&clients_mutex);
            return NULL; 
        }
        username_buffer[bytes_received] = '\0';
        username_buffer[strcspn(username_buffer, "\r\n")] = 0;

        if (strlen(username_buffer) == 0) { 
            send_to_client(client_socket_fd, KRED "[Server]: Kullanıcı adı boş olamaz. Lütfen tekrar deneyin.\n" KNRM);
            // YENİ PROMPT GÖNDERME, istemci zaten yeni giriş için bekleyecek.
            continue; 
        }

        pthread_mutex_lock(&clients_mutex);
        if (!is_username_valid(username_buffer)) {
            send_to_client(client_socket_fd, KRED "[Server]: Geçersiz kullanıcı adı formatı (maks %d karakter, alfanumerik olmalı).\n" KNRM, USERNAME_MAX_LEN);
            server_log("REJECTED", "Geçersiz kullanıcı adı denemesi: '%s' (IP: %s, Slot: %d)", username_buffer, client_ip_copy, client_idx);
            pthread_mutex_unlock(&clients_mutex);
            // YENİ PROMPT GÖNDERME.
            continue; 
        }
        if (is_username_taken(username_buffer, -1)) { 
            send_to_client(client_socket_fd, KRED "[Server]: [ERROR] Username already taken: '%s'. Choose another.\n" KNRM, username_buffer);
            server_log("REJECTED", "Duplicate username attempted: %s (IP: %s, Slot: %d)", username_buffer, client_ip_copy, client_idx); 
            pthread_mutex_unlock(&clients_mutex);
            // YENİ PROMPT GÖNDERME.
            continue; 
        }

        // Kullanıcı adı geçerli ve benzersiz
        add_client_to_system(client_idx, username_buffer); 
        strncpy(current_username_safe, username_buffer, MAX_USERNAME_LEN_INC_NULL-1); 
        current_username_safe[MAX_USERNAME_LEN_INC_NULL-1] = '\0';
        server_log("LOGIN", "user '%s' connected from %s", current_username_safe, client_ip_copy); 
        username_accepted = 1; 
        pthread_mutex_unlock(&clients_mutex);
    } 

    if (!username_accepted) { 
        server_log("INFO", "Kullanıcı adı alınamadan thread sonlanıyor (muhtemelen sunucu kapanıyor) (Slot %d)", client_idx);
        if(client_socket_fd != -1) close(client_socket_fd); 
        pthread_mutex_lock(&clients_mutex);
        clients[client_idx].socket_fd = -1; 
        clients[client_idx].active = 0;
        pthread_mutex_unlock(&clients_mutex);
        return NULL;
    }

    send_to_client(client_socket_fd, KGRN "[Server]: Hoşgeldiniz '%s'! Bağlantı başarılı.\n" KNRM, current_username_safe);

    // Ana komut döngüsü (Bu kısım öncekiyle aynı kalacak)
    while (server_running) {
        pthread_mutex_lock(&clients_mutex);
        int still_active = clients[client_idx].active;
        pthread_mutex_unlock(&clients_mutex);

        if (!still_active || client_socket_fd < 0) { 
            break; 
        }
        
        bytes_received = recv(client_socket_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            buffer[strcspn(buffer, "\r\n")] = 0; 

            char command_full_copy[BUFFER_SIZE]; 
            strncpy(command_full_copy, buffer, sizeof(command_full_copy)-1);
            command_full_copy[sizeof(command_full_copy)-1] = '\0';

            char command[BUFFER_SIZE] = {0};
            char arg1[BUFFER_SIZE] = {0};    
            char arg2[BUFFER_SIZE] = {0};    
            char arg3[BUFFER_SIZE] = {0};    
            char message_body[BUFFER_SIZE] = {0};

            char *p_cmd_parser = buffer; 
            char *token_parser = strsep(&p_cmd_parser, " "); 
            if (token_parser) strncpy(command, token_parser, sizeof(command) -1);

            if (strcmp(command, "/broadcast") == 0) {
                if (p_cmd_parser) strncpy(message_body, p_cmd_parser, sizeof(message_body) -1);
            } else if (strcmp(command, "/whisper") == 0) {
                token_parser = strsep(&p_cmd_parser, " "); 
                if (token_parser) strncpy(arg1, token_parser, sizeof(arg1) -1); 
                if (p_cmd_parser) strncpy(message_body, p_cmd_parser, sizeof(message_body) -1);
            } else if (strcmp(command, "/sendfile_req") == 0) { 
                token_parser = strsep(&p_cmd_parser, " "); 
                if (token_parser) strncpy(arg1, token_parser, sizeof(arg1)-1); 
                token_parser = strsep(&p_cmd_parser, " "); 
                if (token_parser) strncpy(arg2, token_parser, sizeof(arg2)-1); 
                token_parser = strsep(&p_cmd_parser, " "); 
                if (token_parser) strncpy(arg3, token_parser, sizeof(arg3)-1);
            }
            else { 
                token_parser = strsep(&p_cmd_parser, " ");
                if (token_parser) strncpy(arg1, token_parser, sizeof(arg1) -1); 
            }
            
            if (strcmp(command, "/exit") == 0) {
                send_to_client(client_socket_fd, KGRN "[Server]: Disconnected. Goodbye!\n" KNRM); 
                break; 
            } 
            else if (strcmp(command, "/join") == 0) {
                if (arg1[0] == '\0') {
                    send_to_client(client_socket_fd, KRED "[Server]: Kullanım: /join <oda_adı>\n" KNRM);
                } else if (!is_room_name_valid(arg1)) {
                    send_to_client(client_socket_fd, KRED "[Server]: Geçersiz oda adı (sadece alfanumerik, maks %d karakter, boşluksuz).\n" KNRM, ROOM_NAME_MAX_LEN);
                } else {
                    int old_room_idx = -1;
                    pthread_mutex_lock(&clients_mutex);
                    old_room_idx = clients[client_idx].current_room_idx;
                    pthread_mutex_unlock(&clients_mutex);

                    if (old_room_idx != -1) {
                        int same_room = 0;
                        pthread_mutex_lock(&rooms_mutex); 
                        if(old_room_idx < MAX_ROOMS && rooms[old_room_idx].active) { 
                             pthread_mutex_lock(&rooms[old_room_idx].room_mutex); 
                             same_room = (strcmp(rooms[old_room_idx].name, arg1) == 0);
                             pthread_mutex_unlock(&rooms[old_room_idx].room_mutex);
                        }
                        pthread_mutex_unlock(&rooms_mutex);
                        if(same_room){
                            send_to_client(client_socket_fd, KYEL "[Server]: Zaten '%s' odasındasınız.\n" KNRM, arg1);
                            continue; 
                        } 
                        pthread_mutex_lock(&clients_mutex);
                        clients[client_idx].current_room_idx = -1; 
                        pthread_mutex_unlock(&clients_mutex);
                        remove_user_from_room(client_idx, old_room_idx); 
                    }

                    pthread_mutex_lock(&rooms_mutex);
                    int new_room_idx = find_or_create_room(arg1); 
                    pthread_mutex_unlock(&rooms_mutex);

                    if (new_room_idx != -1) {
                        add_user_to_room(client_idx, new_room_idx); 
                        server_log("JOIN", "user '%s' joined room '%s'", current_username_safe, arg1); 
                    } else {
                        send_to_client(client_socket_fd, KRED "[Server]: Oda oluşturulamadı veya sunucuda maksimum oda sayısına ulaşıldı.\n" KNRM);
                    }
                }
            } else if (strcmp(command, "/leave") == 0) {
                 int old_room_idx = -1;
                 pthread_mutex_lock(&clients_mutex);
                 old_room_idx = clients[client_idx].current_room_idx;
                 if (old_room_idx != -1) { 
                    clients[client_idx].current_room_idx = -1; 
                 }
                 pthread_mutex_unlock(&clients_mutex);

                 if (old_room_idx != -1) {
                     remove_user_from_room(client_idx, old_room_idx); 
                     send_to_client(client_socket_fd, KYEL "[Server]: Odadan ayrıldınız.\n" KNRM);
                 } else {
                     send_to_client(client_socket_fd, KYEL "[Server]: Zaten bir odada değilsiniz.\n" KNRM);
                 }
            } else if (strcmp(command, "/broadcast") == 0) {
                 if (message_body[0] == '\0') { 
                    send_to_client(client_socket_fd, KRED "[Server]: Kullanım: /broadcast <mesaj>\n" KNRM);
                } else {
                    int current_room_idx_local = -1;
                    char room_name_copy[MAX_ROOM_NAME_LEN_INC_NULL] = {0};
                    int proceed_to_broadcast = 0; 

                    pthread_mutex_lock(&clients_mutex);
                    current_room_idx_local = clients[client_idx].current_room_idx;
                    pthread_mutex_unlock(&clients_mutex);

                    if (current_room_idx_local != -1) {
                        pthread_mutex_lock(&rooms[current_room_idx_local].room_mutex);
                        if (rooms[current_room_idx_local].active) {
                            strncpy(room_name_copy, rooms[current_room_idx_local].name, MAX_ROOM_NAME_LEN_INC_NULL - 1);
                            room_name_copy[MAX_ROOM_NAME_LEN_INC_NULL - 1] = '\0'; 
                            proceed_to_broadcast = 1;
                            broadcast_message_to_room(current_room_idx_local, message_body, client_idx, current_username_safe);
                        }
                        pthread_mutex_unlock(&rooms[current_room_idx_local].room_mutex); 
                    }

                    if (proceed_to_broadcast) {
                        send_to_client(client_socket_fd, KGRN "[Server]: Message sent to room '%s'\n" KNRM, room_name_copy); 
                        server_log("BROADCAST", "user '%s': %s (to room '%s')", current_username_safe, message_body, room_name_copy); 
                    } else {
                        send_to_client(client_socket_fd, KRED "[Server]: Mesaj göndermek için önce bir odaya katılmalısınız veya oda mevcut değil/aktif değil.\n" KNRM);
                    }
                }
            } else if (strcmp(command, "/whisper") == 0) {
                 if (arg1[0] == '\0' || message_body[0] == '\0') { 
                     send_to_client(client_socket_fd, KRED "[Server]: Kullanım: /whisper <hedef_kullanıcı> <mesaj>\n" KNRM);
                 } else {
                     pthread_mutex_lock(&clients_mutex);
                     int target_client_idx = -1;
                     int target_socket_fd = -1;
                     for (int i = 0; i < MAX_CLIENTS; ++i) {
                         if (clients[i].active && strcmp(clients[i].username, arg1) == 0) {
                             target_client_idx = i;
                             target_socket_fd = clients[i].socket_fd;
                             break;
                         }
                     }
                     pthread_mutex_unlock(&clients_mutex);

                     if (target_client_idx != -1) {
                         if (target_client_idx == client_idx) { 
                              send_to_client(client_socket_fd, KYEL "[Server]: Kendinize özel mesaj gönderemezsiniz.\n" KNRM);
                         } else {
                            send_to_client(target_socket_fd, KYEL "[%s (özel)]: %s\n" KNRM, current_username_safe, message_body);
                            send_to_client(client_socket_fd, KGRN "[Server]: Whisper sent to %s\n" KNRM, arg1); 
                            server_log("COMMAND", "%s sent whisper to %s", current_username_safe, arg1);
                         }
                     } else {
                         send_to_client(client_socket_fd, KRED "[Server]: '%s' adlı kullanıcı bulunamadı veya aktif değil.\n" KNRM, arg1);
                     }
                 }
            }
            else if (strcmp(command, "/sendfile_req") == 0) { 
                server_log("COMMAND", "%s initiated file transfer to %s (file: %s)", current_username_safe, arg2, arg1); 

                if (arg1[0] == '\0' || arg2[0] == '\0' || arg3[0] == '\0') { 
                    send_to_client(client_socket_fd, KRED "[Server]: Dosya transfer isteği için eksik argümanlar.\n" KNRM);
                } else {
                    long filesize = atol(arg3);
                    int target_user_exists = 0;
                    
                    pthread_mutex_lock(&clients_mutex);
                    for(int i=0; i < MAX_CLIENTS; ++i) {
                        if(clients[i].active && strcmp(clients[i].username, arg2) == 0) {
                            target_user_exists = 1;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&clients_mutex); 

                    if (!target_user_exists) {
                        send_to_client(client_socket_fd, KRED "[Server]: Hedef kullanıcı '%s' bulunamadı veya aktif değil.\n" KNRM, arg2);
                        server_log("FILE-ERROR", "File send from '%s' to '%s' for file '%s' failed: Target user '%s' not found.", current_username_safe, arg2, arg1, arg2);
                    } else if (filesize < 0) { 
                        send_to_client(client_socket_fd, KRED "[Server]: Geçersiz dosya boyutu: %ld.\n" KNRM, filesize);
                        server_log("FILE-ERROR", "File '%s' from user '%s' has invalid size: %ld", arg1, current_username_safe, filesize);
                    } else if (filesize > MAX_FILE_SIZE) { 
                        send_to_client(client_socket_fd, KRED "[Server]: Dosya boyutu limiti (%dMB) aşıldı. Dosya: '%s', Boyut: %ld bytes.\n" KNRM, MAX_FILE_SIZE/(1024*1024), arg1, filesize);
                        server_log("ERROR", "File '%s' from user '%s' exceeds size limit (%ld > %d). Rejected.", 
                                    arg1, current_username_safe, filesize, MAX_FILE_SIZE);
                    } else if (!is_file_type_allowed(arg1)) { 
                        send_to_client(client_socket_fd, KRED "[Server]: İzin verilmeyen dosya tipi. İzin verilenler: .txt, .pdf, .jpg, .png. Dosya: '%s'\n" KNRM, arg1);
                        server_log("FILE-ERROR", "File send from '%s': Type not allowed for file '%s'.", current_username_safe, arg1);
                    } else {
                        int q_status = enqueue_file_request(client_idx, current_username_safe, arg2, arg1, filesize);

                        if (q_status > 0) { 
                            server_log("FILE-QUEUE", "Upload '%s' from %s added to queue. Queue size: %d", 
                                       arg1, current_username_safe, q_status); 
                            send_to_client(client_socket_fd, KGRN "[Server]: File '%s' added to the upload queue. Queue size: %d\n" KNRM, arg1, q_status); 
                        } else if (q_status == -6) { 
                             send_to_client(client_socket_fd, KYEL "[Server]: Dosya yükleme kuyruğu şu anda dolu. Lütfen biraz sonra tekrar deneyin.\n" KNRM);
                        } else if (q_status == -1 || q_status == -3) { 
                            send_to_client(client_socket_fd, KYEL "[Server]: Dosya yükleme kuyruğu dolu veya bir sorun var. Lütfen daha sonra tekrar deneyin.\n" KNRM);
                            server_log("FILE-QUEUE", "Upload request for '%s' from '%s' to '%s' failed: Queue full or slot error (status %d).", arg1, current_username_safe, arg2, q_status);
                        } else if (q_status == -5) { 
                            send_to_client(client_socket_fd, KYEL "[Server]: Sunucu kapanmakta olduğu için dosya isteği işlenemedi.\n" KNRM);
                            server_log("FILE-QUEUE", "Upload request for '%s' from '%s' to '%s' failed: Server shutting down.", arg1, current_username_safe, arg2);
                        }
                        else { 
                             send_to_client(client_socket_fd, KRED "[Server]: Dosya yükleme kuyruğuna eklenirken bir sunucu hatası oluştu (kod: %d).\n" KNRM, q_status);
                             server_log("FILE-ERROR", "Failed to enqueue file request from '%s' for '%s' (error code %d).", current_username_safe, arg1, q_status);
                        }
                    }
                }
            }
            else {
                send_to_client(client_socket_fd, KRED "[Server]: Bilinmeyen komut: '%s'\n" KNRM, command_full_copy);
            }
        } else if (bytes_received == 0) { 
            server_log("INFO", "İstemci '%s' (FD: %d) bağlantıyı kapattı (recv 0).", current_username_safe, client_socket_fd);
            break; 
        } else { 
            if (server_running) { 
                pthread_mutex_lock(&clients_mutex);
                int client_still_truly_active = clients[client_idx].active;
                pthread_mutex_unlock(&clients_mutex);
                if (client_still_truly_active && errno != ECONNRESET && errno != EPIPE && errno != EINTR && errno != EBADF) { 
                    server_log("ERROR", "recv hatası istemci '%s' (FD: %d): %s", current_username_safe, client_socket_fd, strerror(errno));
                }
            }
            break; 
        }
    } 
    
    remove_client_from_system(client_idx); 
    server_log("DEBUG", "handle_client thread for slot %d (User: %s) sonlanıyor.", client_idx, current_username_safe);
    return NULL;
}
