// ============================================================================
// BAŞLIK    : AES-256 (256-Bit Key) Çift Blok (32-Bayt) Uzun Veri Birleştirici Alıcı
// DOSYA ADI : 05_AES256_ECB_CiftBlok_32Bayt_UzunVeri_Alici.ino
// AÇIKLAMA  : nRF24L01'in maksimum paket kapasitesi olan 32 baytlık uzun veriyi
//             iki adet 16 baytlık AES-256 bloğu halinde (Blok 1: 0..15 baytlar,
//             Blok 2: 16..31 baytlar) deşifre ederek birleştirir ve tam metni basar.
// ============================================================================

#include <SPI.h>       // SPI haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım tanımlamaları
#include <RF24.h>      // RF24 radyo kontrol kütüphanesi
#include <Crypto.h>    // Kriptografi temel sınıfı
#include <AES.h>       // AES-256 şifreleme kütüphanesi

// Arduino Mega Pin Tanımlamaları
#define CE_PIN  9              // RF24 Chip Enable pini
#define CSN_PIN 53             // RF24 SPI Chip Select pini
#define LED_PIN LED_BUILTIN    // Dahili durum LED'i

RF24 radio(CE_PIN, CSN_PIN);   // Radyo nesnesi
AES256 aes256;                 // AES-256 şifre çözme nesnesi

// 256-bit (32 Bayt) Gizli Anahtar ("kho-256-bit-secure-key-2026-ecb!")
const byte aesKey[32] = {
  'k', 'h', 'o', '-', '2', '5', '6', '-', 'b', 'i', 't', '-', 's', 'e', 'c', 'u',
  'r', 'e', '-', 'k', 'e', 'y', '-', '2', '0', '2', '6', '-', 'e', 'c', 'b', '!'
};

const byte address[6] = "00001"; // Haberleşme boru adresi
unsigned long alinanSayac = 0;   // Alınan paket sayacı

void setup() {
  pinMode(LED_PIN, OUTPUT);      // LED pinini çıkış olarak ayarla
  digitalWrite(LED_PIN, LOW);    // LED'i kapat

  Serial.begin(115200);          // 115200 baud seri haberleşme
  while (!Serial) { ; }          // Seri port hazır olana kadar bekle

  Serial.println(F("\n=================================================="));
  Serial.println(F(" AES-256 32-BAYT (TAM METIN) ALICI (RX) "));
  Serial.println(F("=================================================="));

  // nRF24L01 Modülünü Başlat
  if (!radio.begin()) {
    Serial.println(F("[HATA] nRF24L01 modulu algilanamadi! Kablolari kontrol edin."));
    while (1);
  }

  radio.setChannel(46);          // 2446 MHz çalışma frekansı
  radio.setAutoAck(false);       // Otomatik onay (ACK) kapalı
  radio.setDataRate(RF24_1MBPS); // 1 Mbps veri hızı
  radio.setPALevel(RF24_PA_LOW); // Düşük güç seviyesi

  // PAKET BOYUTUNU TAM 32 BAYT (2 ADET 16 BAYTLIK AES BLOĞU) OLARAK KİLİTLE:
  radio.setPayloadSize(32);

  radio.openReadingPipe(1, address); // Okuma kanalını aç
  radio.startListening();            // Alıcı (RX) moduna geç

  // 256-bit (32 bayt) anahtarı AES çözücüye tanıt
  aes256.setKey(aesKey, sizeof(aesKey));

  Serial.println(F("[OK] Alıcı 32 Baytlık Paketler için Hazır."));
  Serial.println(F("[OK] 2446 MHz frekansında dinleniyor...\n"));
}

void loop() {
  // Havada 32 baytlık şifreli bir paket var mı?
  if (radio.available()) {
    byte gelenSifreli[32] = {0}; // 32 baytlık şifreli (ciphertext) tamponu
    byte cozulenMetin[33] = {0}; // 32 bayt çözülen veri + 1 bayt '\0' string sonlandırıcı

    // 1. Havadan gelen 32 baytın (2 AES bloğunun) TAMAMINI oku
    radio.read(&gelenSifreli, 32);

    digitalWrite(LED_PIN, HIGH); // Paket alındığında LED'i yak
    alinanSayac++;               // Paket sayacını artır

    // 2. --- 1. BLOK DEŞİFRE (0 - 15. Baytlar -> İlk 16 karakter) ---
    aes256.decryptBlock(cozulenMetin, gelenSifreli);

    // 3. --- 2. BLOK DEŞİFRE (16 - 31. Baytlar -> Son 16 karakter) ---
    aes256.decryptBlock(cozulenMetin + 16, gelenSifreli + 16);

    cozulenMetin[32] = '\0'; // Metin bitiş damgası (null-terminator)

    // 4. Birleştirilmiş Tam Metni Seri Port Ekranına Yazdır
    Serial.print(F("[ALINDI #"));
    Serial.print(alinanSayac);
    Serial.print(F("] -> Çözülen Tam Metin: \""));
    Serial.print((char*)cozulenMetin);
    Serial.println(F("\""));

    delay(20);                  // Görsel LED gecikmesi
    digitalWrite(LED_PIN, LOW); // LED'i söndür
  }
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - AES'in standart 16 baytlık blok sınırını aşan daha uzun verilerin (32 bayt
 *      tam nRF24L01 çerçevesi) vericide iki bloğa (0..15 ve 16..31) parçalanarak
 *      şifrelenmesi ve alıcıda ardışık iki `decryptBlock` işlemiyle yeniden tek bir
 *      bütün metin olarak birleştirilmesini (fragmentation & reassembly) hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Tek bir 16 baytlık bloğa sığmayan JSON/CSV sensör paketlerinin, GPS enlem-boylam
 *      dizilerinin veya çok parametreli endüstriyel telemetri mesajlarının paket
 *      bütünlüğü bozulmadan şifreli olarak aktarılmasında kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - İHA / SİHA Telemetri Çerçeveleri (MAVLink Benzeri Yapılar): Bir İHA'nın aynı
 *      anda gönderdiği Enlem, Boylam, İrtifa, Hız, Yönelim (Roll/Pitch/Yaw) ve
 *      Batarya verileri 16 bayttan uzundur. Bu kod, 32 baytlık tam telemetri
 *      çerçevelerinin 256-bit askeri anahtarla iki blok halinde şifrelenip yer
 *      kontrol istasyonunda (YKİ) birleştirilmesini sağlar.
 *    - Taktik Veri Linkleri (Link-16 / JREAP Benzeri Mesaj Parçalama): Uzun taktik
 *      koordinat ve hedef tespit raporlarının alt bloklara bölünerek kriptolu
 *      taşınması ve alıcıda birleştirilmesi süreçlerinin temelini oluşturur.
 * ============================================================================ */
