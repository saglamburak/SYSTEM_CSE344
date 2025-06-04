// Dosya Yolu: final/client/client_network.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h> 
#include <pthread.h>    
#include <errno.h>      
#include <termios.h>    
#include <signal.h> // sig_atomic_t için eklendi

#include "client_network.h"
#include "../common_defs.h" 

// client.c'deki global değişkenlere extern ile erişim
extern volatile sig_atomic_t client_running;
extern char current_prompt[MAX_USERNAME_LEN_INC_NULL + 20]; // common_defs.h'den MAX_USERNAME_LEN_INC_NULL
extern void display_prompt();
extern void clear_current_line();


void *receive_messages(void *socket_fd_ptr_arg) {
    int socket_fd = -1;
    if (socket_fd_ptr_arg != NULL) {
        socket_fd = *((int *)socket_fd_ptr_arg);
        free(socket_fd_ptr_arg); 
    }
    if (socket_fd < 0) {
        fprintf(stderr, KRED "[Receive Thread]: Geçersiz soket FD'si alındı.\n" KNRM);
        pthread_exit(NULL);
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // printf("[DEBUG] Mesaj alma thread'i (FD: %d) başlatıldı.\n", socket_fd);

    while (client_running) { 
        bytes_received = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (!client_running && bytes_received <=0) { 
            break;
        }

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            clear_current_line();
            printf("%s", buffer);
            if (strlen(buffer)>0 && buffer[strlen(buffer)-1] != '\n') {
                printf("\n"); 
            }
            
            if (strstr(buffer, "[Server]: Disconnected. Goodbye!") != NULL || 
                strstr(buffer, "Sunucu kapatılıyor") !=NULL ||
                (strstr(buffer, KRED "[Server]:") !=NULL && 
                 (strstr(buffer, "kullanıcı adı zaten alınmış") !=NULL || 
                  strstr(buffer, "Sunucu dolu")!=NULL ||
                  strstr(buffer, "Geçersiz kullanıcı adı formatı") != NULL 
                  )) 
                ) {
                 client_running = 0; 
            }
            if (client_running && isatty(fileno(stdin))) { // Sadece interaktifse prompt bas
                display_prompt(); 
            }

        } else if (bytes_received == 0) { 
            if (client_running) {
                clear_current_line();
                printf(KRED "\n[INFO] Sunucu bağlantıyı beklenmedik şekilde kapattı.\n" KNRM);
                client_running = 0; 
                // if (isatty(fileno(stdin))) display_prompt(); 
            }
            break; 
        } else { 
            if (client_running) { 
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK && 
                    errno != ECONNRESET && errno != EPIPE && errno != EBADF ) {
                     clear_current_line();
                     printf(KRED "\nMesaj alma hatası: %s\n" KNRM, strerror(errno));
                }
                client_running = 0; 
            }
            if (!client_running) break; 
        }
    }
    // printf("\n[DEBUG] Mesaj alma thread'i (FD: %d) sonlanıyor...\n", socket_fd);
    pthread_exit(NULL);
}
