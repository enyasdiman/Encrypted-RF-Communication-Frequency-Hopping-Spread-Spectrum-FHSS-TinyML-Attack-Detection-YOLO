// ============================================================================
// BAŞLIK    : AES-256 Kriptolu 15 Kanallı FHSS İstemci (Client/TX) - 20 Hz Veri Vericisi
// DOSYA ADI : 11_AES256_FHSS_15Kanalli_Uno_Verici_20Hz.ino
// AÇIKLAMA  : Arduino Uno üzerinde 15 kanallı ortak frekans listesi kullanarak
//             her saniye (1000 ms) kanal atlayan ve saniyede 20 paket (50 ms aralıkla)
//             256-bit AES ile şifrelenmiş veri gönderen verici (Client/TX) kodu.
// ============================================================================

#include <SPI.h>       // SPI donanım haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım tanımlamaları
#include <RF24.h>      // RF24 radyo kontrol kütüphanesi
#include <Crypto.h>    // Kriptografi temel sınıfı
#include <AES.h>       // AESSmall256 / AES-256 blok şifreleme kütüphanesi

// Arduino Uno Pin Tanımları
#define CE_PIN  7              // RF24 Chip Enable pini (Uno: 7)
#define CSN_PIN 8              // RF24 SPI Chip Select pini (Uno: 8)

RF24 radio(CE_PIN, CSN_PIN);   // Radyo nesnesi
AESSmall256 aes256;            // 256-bit AES şifreleme nesnesi

// ALICI (MEGA) İLE BİREBİR AYNI 256-BİT (32 BAYT) ŞİFRELEME ANAHTARI
const uint8_t aes256Anahtari[32] = {
  0x90, 0x28, 0x83, 0xB5, 0xE7, 0x7F, 0x46, 0x78,
  0xFB, 0x2B, 0x3E, 0x6A, 0x91, 0x26, 0xD3, 0xDC,
  0x1A, 0xC3, 0x54, 0x88, 0x9B, 0x02, 0x7E, 0x41,
  0x33, 0xAA, 0xDF, 0x12, 0x67, 0x89, 0xBE, 0xEF
};

// Alıcı ile ortak 5 baytlık boru adresi
const byte pipeAddress[6] = "00001";

// ALICI İLE BİREBİR AYNI 15 KANALLI ORTAK FREKANS LİSTESİ (2476 - 2525 MHz)
const uint8_t kanalListesi[15] = {76, 79, 82, 85, 88, 92, 95, 98, 102, 105, 109, 112, 116, 120, 125};
uint8_t aktifKanalIndex = 0;           // Kanal listesindeki güncel sıra (0..14)
uint8_t sonBasilanKanal = 255;         // Kanal değişimini ekrana tek sefer basmak için değişken

unsigned long t_sifir_zamani = 0;               // Zamanlayıcı başlangıç referansı (ms)
const unsigned long KANAL_ATLAMA_SURESI = 1000; // Kanal değiştirme periyodu: 1000 ms (1 Saniye)

uint32_t gonderilenPaketSayaci = 0;             // Gönderilen toplam paket sayacı

// ALICI İLE BİREBİR AYNI VERİ YAPISI (Toplam 32 Bayt = 2 adet 16 baytlık AES bloğu)
struct __attribute__((packed)) AtlamaliPaket {
  uint8_t hedefKanalIndex; // 1 bayt: Mevcut kanalın listedeki indeksi
  char mesaj[31];          // 31 bayt: Sıra numaralı veri metni ("Veri_No:...")
};

void setup() {
  Serial.begin(9600);      // Seri haberleşmeyi 9600 baud hızında başlat
  delay(1000);

  // nRF24L01 Modülünü Başlat
  if (!radio.begin()) {
    Serial.println(F("HATA: nRF24 baslatilamadi!"));
    while (1);
  }

  // GÖNDERİCİ (CLIENT / TX) RADYO AYARLARI
  radio.setAutoAck(false);            // Otomatik onay (ACK) kapalı
  radio.setPALevel(RF24_PA_MIN);      // Minimum güç seviyesi
  radio.setDataRate(RF24_1MBPS);      // 1 Mbps hava veri hızı

  radio.setChannel(76);               // Başlangıç Kanalı: 76 (2476 MHz)
  radio.openWritingPipe(pipeAddress); // Yazma (TX) borusunu aç
  radio.stopListening();              // Dinlemeyi kapat, Verici (TX) moduna geç

  // 256-bit şifreleme anahtarını AES motoruna yükle
  aes256.setKey(aes256Anahtari, sizeof(aes256Anahtari));

  // Kullanılacak ortak kanal ve frekans listesini başlangıçta raporla
  Serial.println(F("=========================================="));
  Serial.println(F(" KULLANILACAK FREKANS LİSTESİ (MHz)"));
  Serial.println(F("=========================================="));
  for (int i = 0; i < 15; i++) {
    Serial.print(F(" Kanal "));
    Serial.print(kanalListesi[i]);
    Serial.print(F(" -> "));
    Serial.print(2400 + kanalListesi[i]);
    Serial.println(F(" MHz"));
  }
  Serial.println(F("=========================================="));
  Serial.println(F(" VERICI (TX) HAZIR: Veri yayını başlıyor..."));
  Serial.println(F("=========================================="));

  // Alıcı ile senkronize olabilmek için t=0 zamanlayıcısını başlat
  t_sifir_zamani = millis();
}

void loop() {
  unsigned long simdikiZaman = millis();
  unsigned long gecenSure = simdikiZaman - t_sifir_zamani;

  // Geçen zamana göre 15 kanallı listede hangi kanalda olmamız gerektiğini hesapla
  aktifKanalIndex = (gecenSure / KANAL_ATLAMA_SURESI) % 15;
  uint8_t hedefKanal = kanalListesi[aktifKanalIndex];

  // Kanal değişimi gerekiyorsa radyoya uygula
  if (radio.getChannel() != hedefKanal) {
    radio.setChannel(hedefKanal);
  }

  // Sadece kanal değiştiğinde ekrana yazdır (seri portu meşgul etmemek için)
  if (radio.getChannel() != sonBasilanKanal) {
    sonBasilanKanal = radio.getChannel();
    Serial.print(F("-> [FHSS] Verici Kanal Değiştirdi | Güncel Kanal: "));
    Serial.print(sonBasilanKanal);
    Serial.print(F(" ("));
    Serial.print(2400 + sonBasilanKanal);
    Serial.println(F(" MHz)"));
  }

  // 1. Gönderilecek Paketi Hazırla
  AtlamaliPaket paket;
  paket.hedefKanalIndex = aktifKanalIndex;

  // Sıra numaralı veri metni oluştur (Maksimum 30 karakter + '\0' null terminator)
  snprintf(paket.mesaj, 31, "Veri_No:%lu", gonderilenPaketSayaci);

  // 2. Ham Veriyi AES-256 (ECB) ile Şifrele
  uint8_t hamVeri[32] = {0};
  uint8_t sifreliVeri[32] = {0};

  memcpy(hamVeri, &paket, sizeof(AtlamaliPaket)); // Yapıyı 32 baytlık diziye kopyala

  // 32 Baytlık paketi iki adet 16 baytlık AES bloğu halinde şifrele
  aes256.encryptBlock(&sifreliVeri[0], &hamVeri[0]);     // 1. Blok (Bayt 0..15)
  aes256.encryptBlock(&sifreliVeri[16], &hamVeri[16]);   // 2. Blok (Bayt 16..31)

  // 3. Şifreli Veriyi RF Üzerinden Gönder
  radio.write(sifreliVeri, sizeof(sifreliVeri));

  gonderilenPaketSayaci++;

  // Saniyede 20 paket (20 Hz) göndermek için 50 ms bekleme (1000 ms / 20 = 50 ms)
  delay(50);
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - 2 KB SRAM gibi çok kısıtlı bir belleğe sahip olan Arduino Uno (ATmega328P)
 *      üzerinde `AESSmall256` kütüphanesiyle 256-bit askeri düzey şifreleme yapmayı,
 *    - 15 kanallı ortak frekans listesinde (`kanalListesi[15]`) her saniye
 *      otomatik kanal değiştirirken, **saniyede 20 paket (20 Hz / 50 ms periyot)**
 *      yüksek tempolu şifreli telemetri akışı (`Veri_No:...`) basmayı hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Uzaktan kumandalı araçların (RC/Drone) 20 Hz kontrol/telemetri vericisi
 *      (Transmitter), giyilebilir biyomedikal sensör vericileri ve gerçek zamanlı
 *      konum/hız bildiren mobil saha düğümlerinde (Client) kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - İHA / İKA Üzerindeki Hava Veya Saha Vericisi (Airborne / Field Transceiver):
 *      İnsansız araç üzerindeki uçuş/görev bilgisayarının (veya görüntü işleme /
 *      YOLO biriminden gelen hedef tespit çıktılarının) yer kontrol istasyonuna
 *      saniyede 20 kez (20 Hz güncelleme hızıyla) kesintisiz, 256-bit şifreli ve
 *      frekans atlamalı olarak aktarılmasında doğrudan verici düğüm olarak görev yapar.
 *    - Paket Yedekliliği (Time-Diversity / Redundancy): Her 1 saniyelik kanal
 *      slotunda 20 ayrı şifreli paket gönderilmesi, anlık darbe karıştırması
 *      (Pulse Jamming) altında birkaç paket düşse bile o saniye içinde kalan
 *      diğer paketlerin hedefe ulaşmasını garanti altına alır.
 * ============================================================================ */
