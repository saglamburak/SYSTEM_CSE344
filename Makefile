# Dosya Yolu: final/Makefile

CC = gcc
# Daha fazla uyarı için -Wextra, -pedantic eklenebilir. Geliştirme sırasında -g (debug sembolleri) faydalı olur.
CFLAGS = -Wall -Wextra -pthread -g 
LDFLAGS = -pthread

# Hedef çalıştırılabilir dosyalar
SERVER_EXEC = chatserver
CLIENT_EXEC = chatclient

# Sunucu Kaynak Dosyaları
SERVER_OBJS = server/chatserver.o server/client_handler.o server/room_manager.o server/file_transfer.o server/server_utils.o

# İstemci Kaynak Dosyaları
CLIENT_OBJS = client/chatclient.o client/client_network.o

# Ana derleme hedefi
all: $(SERVER_EXEC) $(CLIENT_EXEC)

# Sunucu derleme kuralı (Obje dosyalarından linkleme)
$(SERVER_EXEC): $(SERVER_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Sunucu derlendi: $(SERVER_EXEC)"

# İstemci derleme kuralı (Obje dosyalarından linkleme)
$(CLIENT_EXEC): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "İstemci derlendi: $(CLIENT_EXEC)"

# C kaynak dosyalarından obje dosyası oluşturma kuralları
server/%.o: server/%.c server/globals.h server/server_structs.h server/server_utils.h
	$(CC) $(CFLAGS) -c $< -o $@

client/%.o: client/%.c client/client_network.h ../common_defs.h
	$(CC) $(CFLAGS) -c $< -o $@


# Temizleme kuralı
clean:
	rm -f $(SERVER_EXEC) $(CLIENT_EXEC) server/*.o client/*.o server_log.txt
	@echo "Temizlendi."

.PHONY: all clean