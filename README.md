# SYSTEM_CSE344
Bu projede iletişim, ağ katmanında TCP/IP soketleri üzerinden ve süreç/iş parçacığı yönetimi için standart C kütüphaneleri ve POSIX API'leri (pthread, semaforlar, mutexler) kullanılarak gerçekleştirilmiştir. 
CSE344 - Çoklu İş Parçacıklı Dağıtık Sohbet ve Dosya Sunucusu
Bu proje, CSE344 Sistem Programlama dersi final ödevi kapsamında geliştirilmiş, C programlama dili kullanılarak POSIX thread'leri, TCP soketleri, mutex ve semaforlar ile realize edilmiş çok kullanıcılı bir sohbet ve (simüle edilmiş) dosya paylaşım sistemidir.

Proje Açıklaması
Sistem, merkezi bir sunucu (chatserver) ve bu sunucuya bağlanabilen çok sayıda istemciden (chatclient) oluşur. Sunucu, eş zamanlı olarak birden fazla istemci bağlantısını kabul edebilir ve her bir istemci için ayrı bir iş parçacığı (thread) oluşturarak onlara hizmet verir.

Temel Özellikler:

Kullanıcıların benzersiz isimlerle sunucuya bağlanması.

Kullanıcıların sohbet odaları oluşturabilmesi ve mevcut odalara katılabilmesi.

Oda içinde genel mesajlaşma (/broadcast).

Kullanıcılar arasında özel mesajlaşma (/whisper).

Kullanıcılar arasında dosya gönderme isteği ve bu isteklerin sunucu tarafında bir kuyrukta yönetilmesi (dosya transferi mesaj bazlı simüle edilmiştir).

Sunucu tarafında tüm önemli olayların zaman damgalı olarak loglanması.

Kullanılan Teknolojiler ve Kütüphaneler
Programlama Dili: C (gcc ile derlenmiştir)

Eş Zamanlılık: POSIX Threads (Pthreads)

Ağ Programlama: TCP/IP Soketleri (Berkeley Sockets API)

Senkronizasyon: Mutex Kilitleri (pthread_mutex_t), Semaforlar (sem_t)

Geliştirme ve Test Ortamı: Debian 12 (64-bit) (Proje PDF'inde belirtildiği üzere)

Derleme
Projenin ana dizininde (final/ klasöründe) bulunan Makefile kullanılarak derleme işlemi gerçekleştirilir. Terminalde aşağıdaki komutu çalıştırmanız yeterlidir:

make

Bu komut, chatserver (sunucu) ve chatclient (istemci) adında çalıştırılabilir dosyaları projenin ana dizinine oluşturacaktır.

Derlenmiş dosyaları ve obje dosyalarını temizlemek için:

make clean

Kullanım
Sunucuyu Başlatma
Sunucuyu başlatmak için, derlenmiş chatserver dosyasını bir port numarası argümanıyla çalıştırın:

./chatserver <port_numarasi>

Örnek:

./chatserver 12345

Sunucu, belirtilen portu dinlemeye başlayacak ve logları server_log.txt dosyasına ve konsola yazacaktır.

İstemciyi Başlatma
İstemciyi başlatmak ve sunucuya bağlanmak için, derlenmiş chatclient dosyasını sunucunun IP adresi ve port numarası argümanlarıyla çalıştırın:

./chatclient <sunucu_ip_adresi> <port_numarasi>

Örnek (sunucu aynı makinede çalışıyorsa):

./chatclient 127.0.0.1 12345

Bağlantı kurulduktan sonra, sunucu sizden bir kullanıcı adı girmenizi isteyecektir.

İstemci Komutları
/join <oda_adı>: Belirtilen odaya katılır veya oda yoksa oluşturur.

/leave: Mevcut odadan ayrılır.

/broadcast <mesaj>: Bulunulan odadaki herkese mesaj gönderir.

/whisper <hedef_kullanıcı> <mesaj>: Belirtilen kullanıcıya özel mesaj gönderir.

/sendfile <dosya_adı> <hedef_kullanıcı>: Belirtilen dosyayı belirtilen kullanıcıya gönderme isteği başlatır (dosya transferi simüle edilmiştir). Gönderilecek dosyalar, istemcinin çalıştığı dizindeki test_files_for_sendfile/ klasöründe bulunabilir (test scripti tarafından oluşturulur).

/exit: Sunucudan bağlantıyı keser ve istemciyi sonlandırır.

Proje Yapısı (Özet)
final/
├── Makefile
├── common_defs.h       # Sunucu ve istemci için ortak tanımlamalar
├── server/             # Sunucu kaynak kodları
│   ├── chatserver.c    # Ana sunucu mantığı
│   ├── server_structs.h# Sunucu veri yapıları
│   ├── globals.h       # Global değişken bildirimleri
│   ├── client_handler.c/h # İstemci bağlantılarını işleyen modül
│   ├── room_manager.c/h   # Oda yönetimi modülü
│   ├── file_transfer.c/h  # Dosya transfer kuyruğu ve worker'ları
│   └── server_utils.c/h   # Yardımcı sunucu fonksiyonları
├── client/             # İstemci kaynak kodları
│   ├── chatclient.c    # Ana istemci mantığı
│   └── client_network.c/h # İstemci ağ işlemleri
└── server_log.txt      # Sunucu loglarının tutulduğu dosya
└── test_files_for_sendfile/ # /sendfile testleri için örnek dosyalar (test scripti ile oluşturulur)

Notlar
Bu projedeki dosya transferi, Q&A PDF'inde belirtildiği üzere, gerçek dosya içeriğinin binary transferi yerine mesaj bazlı bir protokol ile simüle edilmiştir. Sunucu, dosya bilgilerini alır, kontrolleri yapar, bir kuyruğa ekler ve (simüle edilmiş) bir işlem süresi sonunda başarılı/başarısız bildirimi yapar. Sunucu tarafında, alıcı için bir klasör yapısı altında (içi temel bilgilerle doldurulmuş) bir dosya oluşturulur.

Sunucu, Ctrl+C (SIGINT) ile kapatıldığında istemcileri bilgilendirmeye ve kaynakları temizlemeye çalışır.

Proje, Debian 12 (64-bit) üzerinde test edilmek üzere tasarlanmıştır.
