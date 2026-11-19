// ============================================================================
// BAŞLIK    : AES-256 (256-Bit Key) Tek Blok (16-Bayt) Kriptolu RF Alıcı
// DOSYA ADI : 04_AES256_ECB_TekBlok_16Bayt_Kriptolu_Alici.ino
// AÇIKLAMA  : 256-bit (32 Bayt) gizli anahtar kullanarak 2446 MHz frekansından
//             gelen 16 baytlık (tek AES bloğu) şifreli paketleri yakalar, hem 
//             havadaki şifreli HEX verisini hem de AES-256 ile çözülen orijinal
//             metni Seri Port ekranına detaylı olarak basar.
// ============================================================================

#include <SPI.h>       // SPI haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ yazmaç tanımları
#include <RF24.h>      // RF24 radyo kontrol kütüphanesi
#include <Crypto.h>    // Kriptografi temel kütüphanesi
#include <AES.h>       // AES-256 blok şifreleme kütüphanesi

// Arduino Mega 2560 Pin Tanımlamaları
#define CE_PIN  9              // RF24 Chip Enable pini
#define CSN_PIN 53             // RF24 SPI Chip Select pini (Mega: 53)
#define LED_PIN LED_BUILTIN    // Paket alım göstergesi LED pini

RF24 radio(CE_PIN, CSN_PIN);   // RF24 radyo nesnesi
AES256 aes256;                 // 256-bit AES Şifre Çözücü Nesnesi

// Vericideki ile BİREBİR AYNI 256-bit (32 Bayt) Gizli Anahtar:
// "kho-256-bit-secure-key-2026-ecb!"
const byte aesKey[32] = {
  'k', 'h', 'o', '-', '2', '5', '6', '-', 'b', 'i', 't', '-', 's', 'e', 'c', 'u',
  'r', 'e', '-', 'k', 'e', 'y', '-', '2', '0', '2', '6', '-', 'e', 'c', 'b', '!'
};

const byte address[6] = "00001"; // 5 baytlık haberleşme boru adresi
unsigned long alinanSayac = 0;   // Alınan toplam şifreli paket sayacı

void setup() {
  pinMode(LED_PIN, OUTPUT);      // LED pinini çıkış yap
  digitalWrite(LED_PIN, LOW);    // Başlangıçta LED'i söndür

  Serial.begin(115200);          // 115200 baud seri port başlat
  while (!Serial) { ; }          // Bağlantı kurulana kadar bekle

  Serial.println(F("\n=================================================="));
  Serial.println(F(" AES-256 (ECB) 2446 MHz SIFRE COZUCU (RX) "));
  Serial.println(F("=================================================="));

  // 1. nRF24L01 Modülünü Başlat ve Donanımı Doğrula
  if (!radio.begin()) {
    Serial.println(F("[HATA] nRF24L01 modulu algilanamadi! Kablolari kontrol edin."));
    while (1); // Donanım hatasında durdur
  }

  radio.setChannel(46);              // 2400 + 46 = 2446 MHz çalışma frekansı
  radio.setAutoAck(false);           // Otomatik onay (ACK) kapalı (vericiyle uyumlu)
  radio.setDataRate(RF24_1MBPS);     // 1 Mbps veri hızı
  radio.setPALevel(RF24_PA_LOW);     // Düşük PA güç seviyesi
  radio.openReadingPipe(1, address); // 1 numaralı okuma borusunu aç
  radio.startListening();            // Alıcı (RX) moduna geç

  // 2. AES-256 Çözme Anahtarını Kripto Motoruna Yükle (32 Bayt = 256 Bit)
  aes256.setKey(aesKey, sizeof(aesKey));

  Serial.println(F("[OK] AES-256 Cozucu Hazir (256-bit Key)."));
  Serial.println(F("[OK] 2446 MHz frekansinda sifreli paketler dinleniyor...\n"));
}

void loop() {
  // Havada yeni bir şifreli paket yakalandı mı?
  if (radio.available()) {
    byte gelenSifreli[16] = {0}; // 16 baytlık (128-bit blok) şifreli veri dizisi
    byte cozulenMetin[17] = {0}; // 16 bayt çözülen veri + 1 bayt '\0' string sonlandırıcı

    // 1. Havadan 16 baytlık şifreli paketi çek
    radio.read(&gelenSifreli, sizeof(gelenSifreli));

    digitalWrite(LED_PIN, HIGH); // Paket alındığında LED'i yak
    alinanSayac++;               // Paket sayacını artır

    // 2. --- AES-256 ECB ŞİFRE ÇÖZME İŞLEMİ ---
    // 16 baytlık şifreli bloğu 256-bit anahtarla tek seferde deşifre et
    aes256.decryptBlock(cozulenMetin, gelenSifreli);
    cozulenMetin[16] = '\0'; // Güvenli karakter dizisi (string) bitişi

    // 3. Sonuçları Seri Port Ekranına Yazdır
    Serial.print(F("[ALINDI #"));
    Serial.print(alinanSayac);
    Serial.println(F("] ----------------------------------"));

    // Havada yakalanan şifreli (ciphertext) veriyi HEX formatında göster
    Serial.print(F(" Havadan Gelen HEX : "));
    for (byte i = 0; i < 16; i++) {
      if (gelenSifreli[i] < 0x10) Serial.print("0"); // Tek haneli HEX değerlerin başına 0 ekle
      Serial.print(gelenSifreli[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    // 256-bit anahtar ile çözülen orijinal metni (plaintext) göster
    Serial.print(F(" AES-256 ile Cozulen Metin : \""));
    Serial.print((char*)cozulenMetin);
    Serial.println(F("\""));
    Serial.println();

    delay(20);                  // Görsel LED beklemesi
    digitalWrite(LED_PIN, LOW); // LED'i söndür
  }
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - En üst düzey simetrik güvenlik standardı olan 256-bit (32 bayt) anahtarlı
 *      AES-256 algoritmasını kullanarak 16 baytlık (128-bit) atomik veri bloklarını
 *      tek çevrimde deşifre etmeyi ve havadaki 16 baytlık tam HEX dökümü ile
 *      çözülmüş açık metni karşılaştırmalı olarak doğrulamayı hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Bankacılık ve finansal işlem onayları, kritik altyapı (enerji şebekesi,
 *      su/doğalgaz vanaları) uzaktan kontrol komutları ve yüksek güvenlikli
 *      endüstriyel IoT kapılarında kaba kuvvet (brute-force) saldırılarına karşı
 *      tam koruma sağlamak için kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - Askeri Düzey (TOP SECRET / NATO Kozmik) Kriptolu Haberleşme: NIST ve NSA
 *      tarafından "Çok Gizli" (Top Secret) seviyedeki askeri verilerin korunması
 *      için onaylanan AES-256 standardı, kuantum bilgisayarların Grover algoritması
 *      saldırılarına karşı dahi 128-bit efektif güvenlik sağlar.
 *    - Kritik Komut ve Füze/Mühimmat Emniyet Bağlantıları: Tek bir 16 baytlık
 *      atomik paket içinde taşınan "Hedef Onay", "Görev İptal" veya "Faydalı Yük
 *      Aktivasyon" gibi kısa ama hayati önem taşıyan askeri komutların sıfır
 *      gecikmeyle ve en yüksek kriptografik güçle iletilmesinde kullanılır.
 * ============================================================================ */
