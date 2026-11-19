// ============================================================================
// BAŞLIK    : AES-256 Kriptolu 15 Kanallı FHSS Sunucu (Server/RX) ve CSV Veri Kaydedici
// DOSYA ADI : 10_AES256_FHSS_15Kanalli_Mega_Alici_ve_CSV_Log.ino
// AÇIKLAMA  : Arduino Mega üzerinde 15 kanallı ortak frekans listesi ile t=0
//             senkronizasyonlu AES-256 (256-bit key) kriptolu FHSS alıcısı.
//             Alınan paketleri CSV formatında ("Log_No,Kanal,Cozulen_Metin")
//             loglar; her 100 pakette bir Anlık Paket Başarım Oranı (PSR) ve
//             1000 pakette bir Kümülatif Ortalama Başarım Raporu üretir.
// ============================================================================

#include <SPI.h>       // SPI donanım haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım tanımlamaları
#include <RF24.h>      // RF24 radyo kontrol kütüphanesi
#include <Crypto.h>    // Kriptografi temel sınıfı
#include <AES.h>       // AESSmall256 / AES-256 blok şifreleme kütüphanesi

// Arduino Mega 2560 Pin Tanımları
#define CE_PIN  9              // RF24 Chip Enable pini
#define CSN_PIN 53             // RF24 SPI Chip Select pini (Mega: 53)

RF24 radio(CE_PIN, CSN_PIN);   // Radyo nesnesi
AESSmall256 aes256;            // Bellek dostu 256-bit AES kripto nesnesi

// Verici (Uno) ile BİREBİR AYNI 256-bit (32 Bayt) Hexadecimal Şifreleme Anahtarı
const uint8_t aes256Anahtari[32] = {
  0x90, 0x28, 0x83, 0xB5, 0xE7, 0x7F, 0x46, 0x78,
  0xFB, 0x2B, 0x3E, 0x6A, 0x91, 0x26, 0xD3, 0xDC,
  0x1A, 0xC3, 0x54, 0x88, 0x9B, 0x02, 0x7E, 0x41,
  0x33, 0xAA, 0xDF, 0x12, 0x67, 0x89, 0xBE, 0xEF
};

// Verici ile ortak 5 baytlık boru (pipe) adresi
const byte pipeAddress[6] = "00001";

// Gruplar arası BİREBİR AYNI 15 Kanallı Ortak Frekans Atlama Listesi (2476 - 2525 MHz)
const uint8_t kanalListesi[15] = {76, 79, 82, 85, 88, 92, 95, 98, 102, 105, 109, 112, 116, 120, 125};
uint8_t aktifKanalIndex = 0;           // Kanal listesindeki güncel sıra (0..14)

unsigned long t_sifir_zamani = 0;      // İlk paketin yakalandığı t=0 senkron başlangıç anı
unsigned long grupBaslangicZamani = 0; // 100 paketlik grubun başlangıç zamanı (ms)
bool senkronizeOlundu = false;         // t=0 senkronizasyon bayrağı

const unsigned long KANAL_ATLAMA_SURESI = 1000; // Her kanalda kalma süresi: 1000 ms (1 Saniye)
const uint16_t HEDEF_GRUP_PAKET = 100;          // Performans raporu için paket grup büyüklüğü
uint16_t alinanPaketSayaci = 0;                 // Mevcut grupta alınan paket sayacı
uint8_t sonBasilanKanal = 255;                  // Kanal değişim bilgisini tek sefer basmak için kontrol değişkeni

float basarimGecmisi[10];                       // 10 grubun (1000 paket) PSR yüzdelerini tutan dizi
uint8_t grupIndeksi = 0;                        // Kaçıncı 100'lük grupta olunduğu (0..9)
unsigned long toplamAlinanPaketGenel = 0;       // Sistem açılışından itibaren alınan toplam paket (CSV Log_No)

// Verici ile BİREBİR AYNI 32 Baytlık Paket Yapısı
struct __attribute__((packed)) AtlamaliPaket {
  uint8_t hedefKanalIndex; // 1 bayt: Vericinin kanal indeksi
  char    mesaj[31];       // 31 bayt: Gönderilen metin yükü -> Toplam = 32 Bayt
};

void setup() {
  Serial.begin(9600);      // Seri haberleşmeyi 9600 baud hızında başlat
  delay(1000);

  // nRF24L01 Modülünü Başlat
  if (!radio.begin()) {
    Serial.println(F("HATA: nRF24 baslatilamadi!"));
    while (1);
  }

  // Alıcı (RX) Radyo Ayarları
  radio.setAutoAck(false);               // Otomatik onay (ACK) kapalı
  radio.setPALevel(RF24_PA_MIN);         // Minimum güç seviyesi
  radio.setDataRate(RF24_1MBPS);         // 1 Mbps veri hızı
  
  radio.setChannel(76);                  // Başlangıç (Baz) Kanalı: 76 (2476 MHz)
  radio.openReadingPipe(0, pipeAddress); // 0 numaralı okuma borusunu aç
  radio.startListening();                // Dinleme (Server/RX) moduna geç

  // 256-bit AES gizli anahtarını kripto motoruna yükle
  aes256.setKey(aes256Anahtari, sizeof(aes256Anahtari));

  Serial.println(F("=========================================="));
  Serial.println(F(" ALICI: Kanal 76'da t=0 Sinyali Bekleniyor "));
  Serial.println(F("=========================================="));

  // İlk şifreli paket gelene kadar Kanal 76'da bekle ve t=0 anını kilitle
  while (!senkronizeOlundu) {
    if (radio.available()) {
      uint8_t gelenSifreli[32] = {0};
      uint8_t cozulmusHam[32] = {0};

      // İlk paketi oku ve AES-256 ile 2 blok halinde çöz
      radio.read(gelenSifreli, sizeof(gelenSifreli));
      aes256.decryptBlock(&cozulmusHam[0], &gelenSifreli[0]);
      aes256.decryptBlock(&cozulmusHam[16], &gelenSifreli[16]);

      // Senkronizasyon başlangıç zamanlarını kaydet
      t_sifir_zamani = millis();
      grupBaslangicZamani = millis();
      senkronizeOlundu = true;
      
      Serial.println(F("\n[t=0 SENKRONİZASYON BAŞARILI] Kesintisiz FHSS ve Ortalama Takibi Başladı!\n"));
      // CSV Log Başlık Satırı (Header)
      Serial.println(F("Log_No,Kanal,Cozulen_Metin"));
    }
  }
}

void loop() {
  // 1. ZAMAN TABANLI SENKRON FREKANS ATLAMA HESABI
  unsigned long simdikiZaman = millis();
  unsigned long gecenSure = simdikiZaman - t_sifir_zamani;

  // Geçen süreye göre 15 kanallı listede hangi kanalda olunması gerektiğini hesapla
  aktifKanalIndex = (gecenSure / KANAL_ATLAMA_SURESI) % 15;
  uint8_t hedefKanal = kanalListesi[aktifKanalIndex];

  // Radyo kanalı hedef kanaldan farklıysa yeni frekansa geç
  if (radio.getChannel() != hedefKanal) {
    radio.setChannel(hedefKanal);
  }

  // Kanal her değiştiğinde bilgi satırı yazdır
  if (radio.getChannel() != sonBasilanKanal) {
    sonBasilanKanal = radio.getChannel();
    Serial.print(F("-> [FHSS] Alıcı Kanal Değiştirdi | Güncel Kanal: "));
    Serial.println(sonBasilanKanal);
  }

  // 2. ŞİFRELİ PAKETLERİ OKUMA, DEŞİFRE ETME VE CSV FORMATINDA LOGLAMA
  while (radio.available()) {
    uint8_t gelenSifreli[32] = {0};
    uint8_t cozulmusHam[32] = {0};

    // Havadan 32 baytlık şifreli paketi oku
    radio.read(gelenSifreli, sizeof(gelenSifreli));

    // AES-256 ile 2x16 bayt blok halinde şifreyi çöz
    aes256.decryptBlock(&cozulmusHam[0], &gelenSifreli[0]);
    aes256.decryptBlock(&cozulmusHam[16], &gelenSifreli[16]);

    // Çözülen ham baytları paket yapısına (struct) kopyala
    AtlamaliPaket alinanPaket;
    memcpy(&alinanPaket, cozulmusHam, 32);
    alinanPaket.mesaj[30] = '\0'; // Güvenli string sonlandırıcı

    alinanPaketSayaci++;
    toplamAlinanPaketGenel++;

    // --- CSV FORMATINDA LOG ÇIKTISI: Log_No,Kanal,"Cozulen_Metin" ---
    Serial.print(toplamAlinanPaketGenel);
    Serial.print(F(","));
    Serial.print(radio.getChannel());
    Serial.print(F(",\""));
    Serial.print(alinanPaket.mesaj);
    Serial.println(F("\""));

    // 3. 100 PAKETLİK GRUP VE 1000 PAKETLİK KÜMÜLATİF BAŞARIM (PSR) RAPORU
    if (alinanPaketSayaci >= HEDEF_GRUP_PAKET) {
      unsigned long toplamGecenSureMs = millis() - grupBaslangicZamani;
      float toplamGecenSureSaniye = toplamGecenSureMs / 1000.0;

      // Beklenen paket sayısı üzerinden Anlık Paket Başarım Oranı (PSR) hesabı
      float beklenenPaketSayisi = toplamGecenSureSaniye * 1.0;
      float basarimOrani = 0.0;
      if (beklenenPaketSayisi > 0) {
        basarimOrani = ((float)HEDEF_GRUP_PAKET / beklenenPaketSayisi) * 100.0;
      }
      if (basarimOrani > 100.0) basarimOrani = 100.0;

      basarimGecmisi[grupIndeksi] = basarimOrani;
      grupIndeksi++;

      Serial.println(F("\n--------------------------------------------------"));
      Serial.print(F(">> 100'LÜK GRUP RAPORU (Grup #")); Serial.print(grupIndeksi); Serial.println(F(")"));
      Serial.print(F("Anlık Paket Başarım Oranı (PSR): %")); Serial.println(basarimOrani, 2);
      Serial.println(F("--------------------------------------------------"));

      // 10 grup (1000 paket) tamamlandığında Kümülatif Ortalama Başarım Raporu bas
      if (grupIndeksi >= 10) {
        float toplamYuzde = 0;
        for (int i = 0; i < 10; i++) {
          toplamYuzde += basarimGecmisi[i];
        }
        float genelOrtalama = toplamYuzde / 10.0;

        Serial.println(F("\n=================================================="));
        Serial.println(F("       1000 PAKETLİK KÜMÜLATİF ÖZET RAPORU        "));
        Serial.println(F("=================================================="));
        Serial.print(F("Önceki 10 Grubun Ortalama Başarımı : %")); Serial.println(genelOrtalama, 2);
        Serial.println(F("==================================================\n"));

        grupIndeksi = 0; // Yeni 1000'lik seri için grup indeksini sıfırla
      }

      // Yeni 100'lük grup için sayaç ve süreyi sıfırla
      alinanPaketSayaci = 0;
      grupBaslangicZamani = millis();
    }
  }
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - Standart Wi-Fi kanallarının (2412-2472 MHz) üzerinde yer alan 2476-2525 MHz
 *      bandındaki 15 ortak kanalda (`kanalListesi[15]`) t=0 anından itibaren
 *      deterministik zaman tabanlı FHSS senkronizasyonu kurmayı,
 *    - Bellek dostu `AESSmall256` motoru ve 256-bit Hex anahtarla şifrelenmiş
 *      paketleri çözerek **`Log_No,Kanal,Cozulen_Metin`** formatında doğrudan
 *      **CSV veri kaydı (Data Logging)** üretmeyi,
 *    - Hem 100 paketlik gruplar halinde Anlık Paket Başarım Oranı (PSR) hem de
 *      1000 paketlik periyotlarda Kümülatif Ortalama Başarım Raporu çıkarmayı hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Bilimsel deney verilerinin, endüstriyel sensör kayıtlarının veya kablosuz
 *      ağ stres testi sonuçlarının doğrudan Python (Pandas/Matplotlib), MATLAB
 *      veya Excel'e aktarılıp grafikleştirilmesi için standart CSV formatında
 *      merkezi sunucu (Server) veri toplama istasyonu olarak kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - Yer Kontrol İstasyonu (YKİ / GCS) Görev Sonu Analiz (AAR - After Action
 *      Review) ve Kara Kutu Kayıt Sistemi: İHA/SİHA uçuşları veya elektronik harp
 *      tatbikatları sırasında alınan her şifreli paketin hangi frekans kanalında
 *      alındığını CSV olarak kaydeder. Görev sonrasında hangi kanallarda paket
 *      düşmesi (Drop Attack / Jamming) yaşandığı bu CSV loglarından tespit edilir.
 *    - Wi-Fi Bandı Dışı (Out-of-Band) Taktik Haberleşme: 76..125 arası kanallar
 *      (2476-2525 MHz) sivil Wi-Fi trafiğinin dışında kaldığı için şehir içi
 *      (meskun mahal) askeri operasyonlarda sivil spektrum kirliliğinden etkilenmeden
 *      yüksek PSR oranıyla haberleşme sağlar.
 * ============================================================================ */
