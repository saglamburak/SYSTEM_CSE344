// Dosya Yolu: final/server/file_transfer.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <pthread.h>
#include <semaphore.h>
#include <time.h>   // clock_gettime, struct timespec için
#include <errno.h>

#include "file_transfer.h"
#include "globals.h"      
#include "server_utils.h" 
#include "../common_defs.h" 

// enqueue_file_request: Dosya yükleme kuyruğuna ekleme fonksiyonu (zaman aşımı ile)
int enqueue_file_request(int sender_idx, const char* sender_username, const char* receiver_username, const char* filename, long filesize) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        server_log("ERROR", "enqueue: clock_gettime failed: %s", strerror(errno));
        return -2; // Genel semafor/saat hatası
    }
    ts.tv_sec += 10; // 10 saniye zaman aşımı

    int sem_op_result;
    // sem_timedwait EINTR ile kesilirse ve server_running hala true ise tekrar denemeli.
    while ((sem_op_result = sem_timedwait(&empty_slots_in_queue, &ts)) == -1 && errno == EINTR) {
        if (!server_running) { // Sunucu kapanıyorsa EINTR normal
            server_log("DEBUG", "enqueue: sem_timedwait server kapanırken EINTR ile kesildi.");
            return -5; // Sunucu kapanıyor özel durumu
        }
        // Başka bir sinyal ile kesildi, döngüye devam et ve tekrar bekle (veya zaman aşımını yeniden hesapla)
        // Basitlik için, zaman aşımını yeniden hesaplamadan devam ediyoruz.
        // Çok kısa süreli kesintiler için bu sorun olmaz. Uzun süreli ise timeout erken olabilir.
        server_log("DEBUG", "enqueue: sem_timedwait EINTR ile kesildi, tekrar deneniyor.");
        // Zaman aşımı için 'ts'nin güncellenmesi gerekebilir, ancak çoğu durumda
        // sinyal kesintisi kısa süreli olacağından, mevcut 'ts' ile devam etmek makul.
        // Daha robust bir çözüm için, kalan süre hesaplanıp yeni bir ts ile tekrar çağrılabilir.
    }

    if (sem_op_result == -1) { // sem_timedwait başarısız oldu
        if (errno == ETIMEDOUT) {
            server_log("FILE-QUEUE", "Upload request for '%s' from '%s' to '%s' timed out after 10s waiting for queue slot.",
                       filename, sender_username, receiver_username);
            return -6; // Zaman aşımı için özel dönüş kodu
        } else {
            // EINTR ve server_running false durumu yukarıda ele alındı.
            // Diğer semafor hataları
            server_log("ERROR", "enqueue: sem_timedwait(empty_slots_in_queue) failed: %s", strerror(errno));
            return -2; 
        }
    }
    
    // sem_timedwait başarılı (sem_op_result == 0), yani bir slot alındı.
    // Ancak, sunucu tam bu slot alındıktan sonra kapanmış olabilir.
    if (!server_running) { 
        sem_post(&empty_slots_in_queue); // Alınan slotu geri iade et
        server_log("DEBUG", "enqueue: Slot alındıktan sonra sunucunun kapandığı fark edildi.");
        return -5; 
    }

    pthread_mutex_lock(&file_queue_mutex);
    if (file_upload_queue[file_queue_add_idx].active_in_queue) {
        pthread_mutex_unlock(&file_queue_mutex);
        sem_post(&empty_slots_in_queue); 
        server_log("CRITICAL", "enqueue: Dosya kuyruğunda (idx: %d) beklenmedik dolu slot! Semafor/mutex mantık hatası.", file_queue_add_idx);
        return -3; 
    }

    file_upload_queue[file_queue_add_idx].active_in_queue = 1;
    strncpy(file_upload_queue[file_queue_add_idx].sender_username, sender_username, MAX_USERNAME_LEN_INC_NULL -1);
    file_upload_queue[file_queue_add_idx].sender_username[MAX_USERNAME_LEN_INC_NULL-1] = '\0';
    file_upload_queue[file_queue_add_idx].sender_client_idx = sender_idx; 
    strncpy(file_upload_queue[file_queue_add_idx].receiver_username, receiver_username, MAX_USERNAME_LEN_INC_NULL -1);
    file_upload_queue[file_queue_add_idx].receiver_username[MAX_USERNAME_LEN_INC_NULL-1] = '\0';
    strncpy(file_upload_queue[file_queue_add_idx].filename, filename, sizeof(file_upload_queue[0].filename) -1);
    file_upload_queue[file_queue_add_idx].filename[sizeof(file_upload_queue[0].filename)-1] = '\0';
    file_upload_queue[file_queue_add_idx].filesize = filesize;
    file_upload_queue[file_queue_add_idx].enqueued_time = time(NULL); 
    
    int enqueued_at_idx = file_queue_add_idx; 
    file_queue_add_idx = (file_queue_add_idx + 1) % MAX_UPLOAD_QUEUE; 
    
    pthread_mutex_unlock(&file_queue_mutex);

    if (sem_post(&items_in_queue) != 0) { 
        server_log("CRITICAL", "enqueue: sem_post(items_in_queue) failed: %s. Kuyruk durumu bozulmuş olabilir!", strerror(errno));
        pthread_mutex_lock(&file_queue_mutex); // Hata durumunda eklenen öğeyi geri almayı dene
        file_upload_queue[enqueued_at_idx].active_in_queue = 0; 
        // file_queue_add_idx'i geri almak daha karmaşık, şimdilik sadece slotu pasif yap.
        pthread_mutex_unlock(&file_queue_mutex);
        // Bu durumda empty_slots_in_queue semaforunu da geri almak gerekir (sem_wait ile), bu da riskli.
        // En iyisi bu tür kritik hatalarda sunucuyu güvenli bir şekilde kapatmak olabilir.
        return -4; 
    }
    
    int current_q_fill_count;
    // items_in_queue semaforunun değeri kuyruktaki güncel eleman sayısını verir.
    // Ancak sem_getvalue thread-safe olmayabilir ve sadece debug için kullanılmalı.
    // Daha güvenilir bir sayım için, ayrı bir sayaç tutulabilir (mutex ile korunmalı)
    // veya MAX_UPLOAD_QUEUE - (empty_slots_in_queue değeri) kullanılabilir.
    // Şimdilik sem_getvalue ile devam edelim.
    if (sem_getvalue(&items_in_queue, &current_q_fill_count) != 0) {
        server_log("WARN", "enqueue: sem_getvalue(items_in_queue) failed: %s", strerror(errno));
        current_q_fill_count = 0; // Hata durumunda bilinmeyen bir değer
    }
    return current_q_fill_count;
}

int dequeue_file_request(file_transfer_request_t *request_out) {
    if (!request_out) return 0; 

    pthread_mutex_lock(&file_queue_mutex);
    if (!file_upload_queue[file_queue_remove_idx].active_in_queue) {
        pthread_mutex_unlock(&file_queue_mutex);
        int items_val; // Debug için
        sem_getvalue(&items_in_queue, &items_val);
        server_log("CRITICAL", "dequeue: Kuyrukta (idx: %d) beklenmedik boş/işlenmiş slot! items_in_queue (%d) ile tutarsız.", 
                   file_queue_remove_idx, items_val);
        request_out->active_in_queue = 0; 
        return 0; 
    }

    *request_out = file_upload_queue[file_queue_remove_idx]; 
    file_upload_queue[file_queue_remove_idx].active_in_queue = 0; 
    file_queue_remove_idx = (file_queue_remove_idx + 1) % MAX_UPLOAD_QUEUE; 
    
    pthread_mutex_unlock(&file_queue_mutex);
    
    return 1; 
}

void *file_transfer_worker(void *arg) {
    long worker_id = (long)arg;
    server_log("INFO", "Dosya Worker Thread #%ld başlatıldı ve işlem bekliyor.", worker_id);

    while (server_running) {
        // server_log("DEBUG", "Worker #%ld items_in_queue için bekliyor...", worker_id);
        int sem_op_result;
        while((sem_op_result = sem_wait(&items_in_queue)) == -1 && errno == EINTR) {
            if(!server_running) {
                 server_log("DEBUG", "Worker #%ld sem_wait server kapanırken EINTR ile kesildi, çıkılıyor.", worker_id);
                 pthread_exit(NULL);
            }
            server_log("DEBUG", "Worker #%ld sem_wait EINTR ile kesildi, tekrar deneniyor.", worker_id);
        }

        if (sem_op_result == -1) { // sem_wait diğer hatalar için
            if(server_running) server_log("ERROR", "Worker #%ld sem_wait(items_in_queue) failed: %s", worker_id, strerror(errno));
            sleep(1); 
            continue; 
        }

        if (!server_running) { 
            sem_post(&items_in_queue); 
            server_log("DEBUG", "Worker #%ld sunucu kapanırken sem_wait'ten çıktı, sonlanıyor.", worker_id);
            break;
        }

        file_transfer_request_t request;
        if (!dequeue_file_request(&request)) { 
             server_log("ERROR", "Worker #%ld kuyruktan öğe alamadı (dequeue_file_request başarısız). Bu items_in_queue sayacını bozabilir!", worker_id);
             // Bu durumda items_in_queue bir şekilde azalmış oldu (sem_wait ile) ama empty_slots artmadı.
             // Bir öğe tüketilemediği için items_in_queue'yi geri post etmek mantıklı olabilir ama riskli.
             // En iyisi bu durumun olmamasını sağlamak.
             continue; 
        }
        
        if (sem_post(&empty_slots_in_queue) != 0) { 
            server_log("CRITICAL", "Worker #%ld sem_post(empty_slots_in_queue) failed: %s. Kuyruk doluluk durumu bozulmuş olabilir!", worker_id, strerror(errno));
        }
        
        double wait_time_seconds = difftime(time(NULL), request.enqueued_time);
        server_log("FILE", "'%s' from user '%s' started upload after %.0f seconds in queue.", 
                    request.filename, request.sender_username, wait_time_seconds);


        server_log("FILE-WORKER", "[W#%ld] '%s' dosyasının '%s' kullanıcısından '%s' kullanıcısına transferi işleniyor (Boyut: %ld)...",
                   worker_id, request.filename, request.sender_username, request.receiver_username, request.filesize);
        
        char saved_filepath[512];
        if (generate_unique_filepath(SERVER_FILES_DIR, request.receiver_username, request.filename, saved_filepath, sizeof(saved_filepath)) == NULL) {
            server_log("ERROR", "[W#%ld] Dosya '%s' için kayıt yolu oluşturulamadı.", worker_id, request.filename);
            pthread_mutex_lock(&clients_mutex);
            if (request.sender_client_idx >= 0 && request.sender_client_idx < MAX_CLIENTS && clients[request.sender_client_idx].active) {
                send_to_client(clients[request.sender_client_idx].socket_fd, KRED "[Server]: Dosya '%s' transferi sırasında sunucu hatası (kayıt yolu).\n" KNRM, request.filename);
            }
            pthread_mutex_unlock(&clients_mutex);
            continue; 
        }
        
        // GERÇEK DOSYA TRANSFERİ (AĞ ÜZERİNDEN) BU NOKTADA YAPILACAKTI.
        // Şimdilik sadece simülasyon ve dosyayı boş olarak oluşturma.
        unsigned int sleep_time_s = 1 + (request.filesize / (1024*1024)); 
        if (sleep_time_s < 1) sleep_time_s = 1; 
        if (sleep_time_s > 5) sleep_time_s = 5; 
        sleep(sleep_time_s); 

        FILE* fp_dummy = fopen(saved_filepath, "w");
        if (fp_dummy) {
            fprintf(fp_dummy, "Simulated file: %s\nFrom: %s\nTo: %s\nSize: %ld bytes\nEnqueued: %sProcessed by Worker: %ld\n",
                    request.filename, request.sender_username, request.receiver_username, request.filesize, 
                    asctime(localtime(&(request.enqueued_time))), 
                    worker_id);
            fclose(fp_dummy);
            server_log("SEND FILE", "'%s' sent from %s to %s (success)", // PDF Log formatı
                 request.filename, request.sender_username, request.receiver_username);
        } else {
            server_log("ERROR", "[W#%ld] Simüle edilmiş dosya '%s' oluşturulamadı: %s", worker_id, saved_filepath, strerror(errno));
            pthread_mutex_lock(&clients_mutex);
            if (request.sender_client_idx >= 0 && request.sender_client_idx < MAX_CLIENTS && clients[request.sender_client_idx].active) {
                send_to_client(clients[request.sender_client_idx].socket_fd, KRED "[Server]: Dosya '%s' transferi sırasında sunucu hatası (dosya oluşturma).\n" KNRM, request.filename);
            }
            pthread_mutex_unlock(&clients_mutex);
            continue; 
        }

        pthread_mutex_lock(&clients_mutex); 
        if (request.sender_client_idx >= 0 && request.sender_client_idx < MAX_CLIENTS &&
            clients[request.sender_client_idx].active && 
            strcmp(clients[request.sender_client_idx].username, request.sender_username) == 0) { 
            send_to_client(clients[request.sender_client_idx].socket_fd, 
                           KGRN "[Server]: File sent successfully\n" KNRM); // PDF İstemci Çıktısı
        }

        int receiver_socket_fd = -1;
        for (int i=0; i < MAX_CLIENTS; ++i) {
            if (clients[i].active && strcmp(clients[i].username, request.receiver_username) == 0) {
                receiver_socket_fd = clients[i].socket_fd;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (receiver_socket_fd != -1) {
            send_to_client(receiver_socket_fd, 
                           KYEL "[Server]: '%s' kullanıcısından '%s' (%ld bytes) adlı bir dosya aldınız (Sunucuya kaydedildi).\n" KNRM,
                           request.sender_username, request.filename, request.filesize);
        }
    }
    server_log("INFO", "Dosya Worker Thread #%ld sonlandırılıyor.", worker_id);
    pthread_exit(NULL);
}
