// Dosya Yolu: final/client/chatclient.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h> 
#include <errno.h>
#include <termios.h> 
#include <ctype.h> 

#include "client_network.h" 
#include "../common_defs.h" 

// Global değişkenler
volatile sig_atomic_t client_running = 1;
int main_socket_fd = -1;
char current_prompt[MAX_USERNAME_LEN_INC_NULL + 20] = "> "; 
char logged_in_username[MAX_USERNAME_LEN_INC_NULL] = {0};
pthread_t recv_thread_id_global = 0; 


void display_prompt() {
    printf("%s", current_prompt);
    fflush(stdout);
}

void clear_current_line() {
    if (isatty(fileno(stdout))) { 
        printf("\r%*s\r", 80, ""); 
    }
}

void sigint_handler_client(int signum) {
    (void)signum; 
    if (client_running) { 
        client_running = 0; 
        char msg[] = "\n[INFO] Ctrl+C algılandı. /exit gönderiliyor...\n";
        write(STDOUT_FILENO, msg, strlen(msg)); 
        if (main_socket_fd != -1) {
            char exit_cmd[] = "/exit"; 
            send(main_socket_fd, exit_cmd, strlen(exit_cmd), MSG_NOSIGNAL); 
            shutdown(main_socket_fd, SHUT_RDWR); 
        }
    }
}


int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    char server_response_buffer[BUFFER_SIZE]; 

    if (argc != 3) {
        fprintf(stderr, "Kullanım: ./chatclient <server_ip> <port>\n");
        exit(EXIT_FAILURE);
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);

    if (port < 1024 || port > 65535) {
        fprintf(stderr, "Geçersiz port numarası: %s.\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sa.sa_handler = sigint_handler_client;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT error");
        exit(EXIT_FAILURE);
    }

    main_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (main_socket_fd == -1) {
        perror("Soket oluşturulamadı");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(port);

    if (connect(main_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Sunucuya bağlanılamadı");
        if(main_socket_fd != -1) { close(main_socket_fd); main_socket_fd = -1;}
        exit(EXIT_FAILURE);
    }
    printf(KGRN "[INFO] %s:%d adresindeki sunucuya bağlandı.\n" KNRM, server_ip, port);

    // Sunucudan ilk kullanıcı adı isteme mesajını al ve bas
    ssize_t bytes = recv(main_socket_fd, server_response_buffer, BUFFER_SIZE - 1, 0);
    if (bytes > 0) {
        server_response_buffer[bytes] = '\0';
        printf("%s", server_response_buffer); 
    } else {
        fprintf(stderr, KRED "Sunucudan ilk prompt alınamadı veya bağlantı kapandı.\n" KNRM);
        if(main_socket_fd != -1) {close(main_socket_fd); main_socket_fd = -1;}
        exit(EXIT_FAILURE);
    }

    int username_accepted_by_server = 0;
    while(client_running && !username_accepted_by_server) {
        printf("> "); // İstemci kendi prompt'unu basar
        fflush(stdout);
        if (fgets(logged_in_username, MAX_USERNAME_LEN_INC_NULL, stdin) == NULL) {
             if (errno == EINTR && !client_running) { break; }
             if (client_running) { 
                clear_current_line();
                printf("EOF algılandı. Çıkılıyor...\n");
                client_running = 0; 
             }
             break;
        }
        logged_in_username[strcspn(logged_in_username, "\r\n")] = 0; 

        if (!client_running) break; 

        if (strlen(logged_in_username) == 0) { 
            printf(KRED "Kullanıcı adı boş olamaz. Lütfen tekrar deneyin.\n" KNRM);
            // Sunucuya boş string göndermiyoruz, istemci tekrar prompt basacak.
            continue; 
        }
        // İstemci tarafı format kontrolü yapılabilir ama sunucu zaten yapacak.
        // int is_valid_local = 1;
        // ...

        if (send(main_socket_fd, logged_in_username, strlen(logged_in_username), 0) < 0) {
            perror("Kullanıcı adı gönderilemedi");
            client_running = 0; 
            break;
        }

        // Sunucudan yanıtı al (onay veya sadece hata mesajı)
        bytes = recv(main_socket_fd, server_response_buffer, BUFFER_SIZE - 1, 0);
        if (bytes > 0) {
            server_response_buffer[bytes] = '\0';
            printf("%s", server_response_buffer); // Sunucudan gelen mesajı bas
            
            if (strstr(server_response_buffer, KGRN "[Server]: Hoşgeldiniz") != NULL && 
                strstr(server_response_buffer, logged_in_username) != NULL) {
                username_accepted_by_server = 1; // Başarılı, döngüden çık
                snprintf(current_prompt, sizeof(current_prompt), KBLU "[%s]" KNRM "> ", logged_in_username);
            } 
            // Eğer hata mesajı geldiyse (örn: "Username already taken"),
            // valid_username_entered = 0 kalır ve döngü başa döner.
            // İstemci yeni bir "> " prompt basar ve kullanıcıdan yeni girdi bekler.
            // Sunucu da zaten `continue` ile döngüsünün başına dönüp yeni `recv` bekliyor olacak.
            else if (strstr(server_response_buffer, "Sunucu dolu")) { // Sunucu doluysa çık
                client_running = 0;
                break;
            }
        } else { 
            fprintf(stderr, KRED "\nKullanıcı adı yanıtı alınamadı veya bağlantı kapandı.\n" KNRM);
            client_running = 0; 
            break;
        }
    } 

    if (!client_running || !username_accepted_by_server) { 
         if (main_socket_fd != -1) { close(main_socket_fd); main_socket_fd = -1;}
         fprintf(stderr, KRED "Giriş başarısız veya işlem iptal edildi. Program sonlandırılıyor.\n" KNRM);
         exit(1); 
    }

    // Mesaj alma thread'ini başlat
    int *p_socket_fd_recv = malloc(sizeof(int));
     if (!p_socket_fd_recv) {
        perror("Thread argümanı için bellek ayrılamadı (recv)");
        if(main_socket_fd != -1) {close(main_socket_fd); main_socket_fd = -1;}
        exit(EXIT_FAILURE);
    }
    *p_socket_fd_recv = main_socket_fd;

    if (pthread_create(&recv_thread_id_global, NULL, receive_messages, (void *)p_socket_fd_recv) != 0) {
        perror("Mesaj alma thread'i oluşturulamadı");
        free(p_socket_fd_recv); 
        if(main_socket_fd != -1) {close(main_socket_fd); main_socket_fd = -1;}
        exit(EXIT_FAILURE);
    }
    
    printf("Bağlantı başarılı. Komutlar: /join <oda>, /leave, /broadcast <mesaj>, /whisper <kisi> <mesaj>, /sendfile <dosya> <kisi>, /exit\n");
    
    // Ana gönderi döngüsü (Bu kısım öncekiyle aynı kalabilir)
    char input_buffer[BUFFER_SIZE];
    while (client_running) {
        display_prompt(); 
        if (fgets(input_buffer, BUFFER_SIZE, stdin) == NULL) { 
            if (errno == EINTR && !client_running) { break; }
            if(client_running) { 
                clear_current_line();
                printf("EOF algılandı. /exit komutu gönderiliyor...\n");
                strncpy(input_buffer, "/exit", BUFFER_SIZE-1); 
                input_buffer[BUFFER_SIZE-1] = '\0';
                 if (send(main_socket_fd, input_buffer, strlen(input_buffer), MSG_NOSIGNAL) < 0) { }
                client_running = 0; 
            }
            break; 
        }
        input_buffer[strcspn(input_buffer, "\r\n")] = 0; 

        if (!client_running) break; 

        if (strlen(input_buffer) > 0) {
            if (strncmp(input_buffer, "/sendfile ", 10) == 0) {
                char filename[256] = {0};
                char target_user[MAX_USERNAME_LEN_INC_NULL] = {0}; 
                if (sscanf(input_buffer, "/sendfile %255s %16s", filename, target_user) == 2) { 
                    struct stat file_stat;
                    if (stat(filename, &file_stat) == 0) { 
                        if (S_ISDIR(file_stat.st_mode)) { 
                            clear_current_line();
                            printf(KRED "[Client Hata]: '%s' bir klasör, dosya değil.\n" KNRM, filename);
                        } else {
                            long filesize = file_stat.st_size;
                            char file_req_cmd[BUFFER_SIZE];
                            snprintf(file_req_cmd, BUFFER_SIZE, "/sendfile_req %s %s %ld", filename, target_user, filesize);
                            if (send(main_socket_fd, file_req_cmd, strlen(file_req_cmd), MSG_NOSIGNAL) < 0) {
                                 if(client_running) { clear_current_line(); perror("Dosya transfer isteği gönderilemedi");}
                                 client_running = 0; break; 
                            }
                            clear_current_line(); 
                            printf(KYEL "[INFO] Dosya transfer isteği '%s' kullanıcısına '%s' için gönderildi (Boyut: %ld bytes).\n" KNRM, target_user, filename, filesize);
                        }
                    } else {
                        clear_current_line();
                        printf(KRED "[Client Hata]: Dosya '%s' bulunamadı veya erişilemiyor: %s\n" KNRM, filename, strerror(errno));
                    }
                } else {
                    clear_current_line();
                    printf(KRED "[Client Hata]: Kullanım: /sendfile <dosyaadı> <hedef_kullanıcı>\n" KNRM);
                }
            } else { 
                if (send(main_socket_fd, input_buffer, strlen(input_buffer), MSG_NOSIGNAL) < 0) {
                    if(client_running) { clear_current_line(); perror("Mesaj gönderilemedi");}
                    client_running = 0; 
                    break;
                }
                if (strcmp(input_buffer, "/exit") == 0) {
                    client_running = 0; 
                    break; 
                }
            }
        }
    } 

    client_running = 0; 
    if (main_socket_fd != -1) {
        if (shutdown(main_socket_fd, SHUT_RDWR) != 0 && errno != ENOTCONN) { }
    }
    
    if (recv_thread_id_global != 0) { 
        pthread_join(recv_thread_id_global, NULL); 
    }
    
    if (main_socket_fd != -1) {
        close(main_socket_fd);
        main_socket_fd = -1; 
    }
    clear_current_line(); 
    printf(KGRN "\n[INFO] Bağlantı sonlandırıldı.\n" KNRM);
    return 0;
}
