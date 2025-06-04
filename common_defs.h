// Dosya Yolu: final/common_defs.h

#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#define BUFFER_SIZE 1024
#define USERNAME_MAX_LEN 16
#define MAX_USERNAME_LEN_INC_NULL (USERNAME_MAX_LEN + 1)

#define ROOM_NAME_MAX_LEN 32
#define MAX_ROOM_NAME_LEN_INC_NULL (ROOM_NAME_MAX_LEN + 1)

// Renk kodları (isteğe bağlı, terminal destekliyorsa)
#define KGRN  "\x1B[32m" // Yeşil (Başarı)
#define KRED  "\x1B[31m" // Kırmızı (Hata)
#define KYEL  "\x1B[33m" // Sarı (Uyarı/Bilgi)
#define KBLU  "\x1B[34m" // Mavi
#define KNRM  "\x1B[0m"  // Normal (Rengi sıfırla)

// Sunucudan istemciye dosya transferi için özel mesajlar
#define SERVER_READY_FOR_FILE_MSG_PREFIX "[SERVER_READY_FOR_FILE]"
#define FILE_CHUNK_MSG_PREFIX          "[FILE_CHUNK]" // İsteğe bağlı, eğer veri ve kontrol mesajları karışacaksa
#define FILE_TRANSFER_COMPLETE_MSG     "[FILE_TRANSFER_COMPLETE_BY_CLIENT]"
#define FILE_TRANSFER_ERROR_MSG        "[FILE_TRANSFER_ERROR_BY_CLIENT]"


#endif // COMMON_DEFS_H