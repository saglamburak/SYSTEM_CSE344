// Dosya Yolu: final/server/server_utils.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/socket.h> 
#include <sys/stat.h>   
#include <sys/types.h>  
#include <errno.h>      

#include "server_utils.h"
#include "globals.h" 
#include "../common_defs.h" 

void server_log(const char *level, const char *message_format, ...) {
    time_t now = time(NULL);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    char log_prefix[100];
    snprintf(log_prefix, sizeof(log_prefix), "%s - [%s] ", time_buf, level);
    
    char log_content[BUFFER_SIZE * 2];
    va_list args;
    va_start(args, message_format);
    vsnprintf(log_content, sizeof(log_content), message_format, args);
    va_end(args);
    
    printf("%s%s\n", log_prefix, log_content); 
    if (log_file) {
        fprintf(log_file, "%s%s\n", log_prefix, log_content);
        fflush(log_file); 
    }
}

void send_to_client(int client_socket_fd, const char* message_format, ...) {
    if (client_socket_fd < 0) {
        // fprintf(stderr, "[DEBUG SEND_SKIP] Skipped send to invalid FD %d\n", client_socket_fd);
        return; 
    }
    char buffer[BUFFER_SIZE];
    va_list args;
    va_start(args, message_format);
    vsnprintf(buffer, BUFFER_SIZE, message_format, args);
    va_end(args);

    // fprintf(stderr, "[DEBUG SEND_ATTEMPT] Attempting to send to FD %d: [[%s]]\n", client_socket_fd, buffer);
    ssize_t bytes_sent = send(client_socket_fd, buffer, strlen(buffer), MSG_NOSIGNAL); 
    
    if (bytes_sent < 0) { 
        // fprintf(stderr, KRED "[DEBUG SEND_FAIL] Failed to send to FD %d: %s (errno %d). Message: [[%s]]\n" KNRM, 
        //         client_socket_fd, strerror(errno), errno, buffer);
        if (errno != EPIPE && errno != ECONNRESET) { 
            // server_log("WARN", "send_to_client error to FD %d: %s", client_socket_fd, strerror(errno)); 
        }
    } else if (bytes_sent < (ssize_t)strlen(buffer)) {
        // fprintf(stderr, KYEL "[DEBUG SEND_PARTIAL] Partially sent to FD %d: %ld of %zu bytes. Message: [[%s]]\n" KNRM,
        //         client_socket_fd, bytes_sent, strlen(buffer), buffer);
        server_log("WARN", "Partially sent %ld of %zu bytes to FD %d", bytes_sent, strlen(buffer), client_socket_fd);
    }
}

int is_username_valid(const char *username) {
    size_t len = strlen(username);
    if (len == 0 || len > USERNAME_MAX_LEN) return 0;
    for (size_t i = 0; i < len; ++i) {
        if (!isalnum((unsigned char)username[i])) return 0;
    }
    return 1;
}

int is_room_name_valid(const char *room_name) {
    size_t len = strlen(room_name);
    if (len == 0 || len > ROOM_NAME_MAX_LEN) return 0;
    for (size_t i = 0; i < len; ++i) {
        if (!isalnum((unsigned char)room_name[i])) return 0;
    }
    return 1;
}

int is_file_type_allowed(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename || strlen(dot) == 1) return 0; 
    dot++; 
    const char* allowed_types[] = {"txt", "pdf", "jpg", "png", NULL}; 
    for (int i = 0; allowed_types[i]; ++i) {
        if (strcasecmp(dot, allowed_types[i]) == 0) return 1;
    }
    server_log("DEBUG", "Reddedilen dosya tipi: '.%s' (dosyaadı: %s)", dot, filename);
    return 0;
}

int ensure_directory_exists(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) != 0) { 
            server_log("ERROR", "Klasör oluşturulamadı '%s': %s", path, strerror(errno));
            return 0;
        }
        server_log("DEBUG", "Klasör oluşturuldu: '%s'", path);
    } else if (!S_ISDIR(st.st_mode)) {
        server_log("ERROR", "'%s' bir klasör değil.", path);
        return 0;
    }
    return 1;
}

char* generate_unique_filepath(const char* base_dir, const char* receiver_username, const char* original_filename, char* out_filepath, size_t out_filepath_size) {
    char receiver_dir[512];
    snprintf(receiver_dir, sizeof(receiver_dir), "%s/%s", base_dir, receiver_username);
    if (!ensure_directory_exists(receiver_dir)) {
        return NULL; 
    }

    char filename_no_ext[200] = {0};
    char ext[50] = {0};
    const char *dot = strrchr(original_filename, '.');
    if (dot && dot != original_filename) {
        strncpy(filename_no_ext, original_filename, dot - original_filename);
        filename_no_ext[dot - original_filename] = '\0'; 
        strncpy(ext, dot, sizeof(ext)-1);
        ext[sizeof(ext)-1] = '\0';
    } else {
        strncpy(filename_no_ext, original_filename, sizeof(filename_no_ext)-1);
        filename_no_ext[sizeof(filename_no_ext)-1] = '\0';
    }

    snprintf(out_filepath, out_filepath_size, "%s/%s", receiver_dir, original_filename);
    struct stat st;
    int counter = 0;
    while (stat(out_filepath, &st) == 0) { 
        counter++;
        snprintf(out_filepath, out_filepath_size, "%s/%s_%d%s", receiver_dir, filename_no_ext, counter, ext);
        if (counter > 100) { 
            server_log("ERROR", "Benzersiz dosya adı '%s' için çok fazla deneme yapıldı.", original_filename);
            return NULL;
        }
    }
    if (counter > 0) { 
        const char *actual_filename_only = strrchr(out_filepath, '/');
        if (actual_filename_only) actual_filename_only++; else actual_filename_only = out_filepath;
        // PDF Log: [FILE] Conflict: 'project.pdf' received twice -> renamed 'project_1.pdf'
        // "received twice" bilgisini burada doğrudan bilemeyiz, genel bir çakışma logu atalım.
        server_log("FILE", "Conflict: '%s' -> renamed '%s'", original_filename, actual_filename_only);
    }
    return out_filepath;
}
