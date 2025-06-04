// Dosya Yolu: final/server/file_transfer.h

#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include "server_structs.h" // file_transfer_request_t için

// Prototip Bildirimleri
void *file_transfer_worker(void *arg);
int enqueue_file_request(int sender_idx, const char* sender_username, const char* receiver_username, const char* filename, long filesize);
int dequeue_file_request(file_transfer_request_t *request_out);

#endif // FILE_TRANSFER_H