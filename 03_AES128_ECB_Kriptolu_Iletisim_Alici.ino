// ============================================================================
// BAŞLIK    : AES-128 ECB Kriptolu RF Alıcı (32-Bayt Çift Blok Şifre Çözücü)
// DOSYA ADI : 03_AES128_ECB_Kriptolu_Iletisim_Alici.ino
// AÇIKLAMA  : 2446 MHz frekansında 128-bit ("kho-128-bit-key!") gizli anahtarı
//             ile şifrelenmiş 32 baytlık paketleri alır, iki adet 16 baytlık 
//             AES-128 ECB bloğu halinde deşifre eder ve RPD/RSSI durumuyla raporlar.
// ============================================================================

#include <SPI.h>       // SPI haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım tanımlamaları
#include <RF24.h>      // RF24 kontrol kütüphanesi
#include <Crypto.h>    // Kriptografi temel sınıfı
#include <AES.h>       // AES şifreleme/çözme kütüphanesi

// --- KULLANILAN ALICI KARTA GÖRE PİN SEÇİMİ ---
// Arduino Uno/Nano ise CSN_PIN = 10, Arduino Mega ise CSN_PIN = 53 olmalıdır.
#define CE_PIN   9             // RF24 Chip Enable pini
#define CSN_PIN 53             // RF24 SPI Chip Select pini
#define LED_PIN LED_BUILTIN    // Paket alımında yanıp sönecek dahili LED

RF24 radio(CE_PIN, CSN_PIN);   // Radyo nesnesi

// Verici ile BİREBİR AYNI 5 baytlık haberleşme adresi
const byte address[6] = "00001";

// 128-bit (16 Bayt) Gizli Anahtar: "kho-128-bit-key!" (Vericiyle aynı olmak zorundadır)
const byte aesKey[16] = {
  'k', 'h', 'o', '-', '1', '2', '8', '-', 'b', 'i', 't', '-', 'k', 'e', 'y', '!'
};

AES128 aes128;                        // AES-128 ECB şifre çözme nesnesi
unsigned long alinanPaketSayisi = 0;  // Alınan toplam paket sayacı

// ----------------------------------------------------------------------------
// FONKSİYON : cozECB
// GÖREVİ    : 32 baytlık şifreli RF paketini 2x16 bayt blok halinde AES-128 ECB
//             modunda çözerek açık metin (plaintext) dizisine yazar.
// ----------------------------------------------------------------------------
void cozECB(byte* sifreliVeri, byte* cozulmusVeri, const byte* key) {
  aes128.setKey(key, 16);                                   // 128-bit anahtarı AES motoruna yükle
  aes128.decryptBlock(cozulmusVeri, sifreliVeri);           // İlk 16 baytı çöz (Blok 1: Bayt 0..15)
  aes128.decryptBlock(cozulmusVeri + 16, sifreliVeri + 16); // İkinci 16 baytı çöz (Blok 2: Bayt 16..31)
}

void setup() {
  pinMode(LED_PIN, OUTPUT);     // LED pinini çıkış olarak ayarla
  digitalWrite(LED_PIN, LOW);   // LED'i başlangıçta söndür

  Serial.begin(115200);         // Seri haberleşmeyi 115200 baud hızında başlat
  while (!Serial) { ; }         // Seri port açılana kadar bekle

  Serial.println(F("\n========================================================"));
  Serial.println(F(" 2446 MHz 'kho-merhaba' AES-128 ECB ALICI (RX) TESTI    "));
  Serial.println(F("========================================================"));

  // 1. nRF24L01 Modülünü Başlat
  if (!radio.begin()) {
    Serial.println(F("[HATA ❌] Alici nRF24L01 modulu algilanamadi! Kablolari kontrol edin."));
    while (1); // Modül algılanamazsa kilitlen
  }
  Serial.println(F("[OK ✔] nRF24L01 Modulu basariyla baslatildi."));

  // 2. Verici ile Eşleşen RF Ayarları
  radio.setChannel(46);           // 2400 + 46 = 2446 MHz kanal frekansı
  radio.setAutoAck(false);        // Verici ile uyumlu (Otomatik ACK kapalı)
  radio.setDataRate(RF24_1MBPS);  // 1 Mbps hava veri hızı
  radio.setPALevel(RF24_PA_LOW);  // Kararlı yakın mesafe iletişimi için düşük PA gücü

  // 3. Okuma Kanalını Aç ve Dinlemeye Başla
  radio.openReadingPipe(1, address); // 1. boruyu "00001" adresine bağla
  radio.startListening();            // Alıcı (RX) moduna geç

  Serial.print(F("[BILGI] Dinleme Frekansi: "));
  Serial.print(2400 + radio.getChannel());
  Serial.println(F(" MHz | Kripto: AES-128 ECB"));
  Serial.println(F("[BILGI] Sifreli sinyal bekleniyor... Lutfen vericiyi calistirin.\n"));
}

void loop() {
  // Havada okunmaya hazır bir paket var mı?
  if (radio.available()) {
    byte gelenSifreliMesaj[32]; // 32 baytlık şifreli veri tamponu
    byte cozulmusMesaj[33];     // Sonuna güvenli null-terminator ('\0') eklemek için +1 bayt
    memset(cozulmusMesaj, 0, sizeof(cozulmusMesaj));

    // 32 baytlık şifreli veriyi radyodan oku
    radio.read(&gelenSifreliMesaj, sizeof(gelenSifreliMesaj));

    // AES-128 ECB ile şifreyi çöz
    cozECB(gelenSifreliMesaj, cozulmusMesaj, aesKey);
    cozulmusMesaj[32] = '\0'; // Metin taşmasını önlemek için string sonlandırıcı

    alinanPaketSayisi++;        // Paket sayacını artır
    digitalWrite(LED_PIN, HIGH); // Görsel onay için LED'i yak

    // Seri Port Ekranına çözülen paketi yazdır
    Serial.print(F("[ALINDI ✔] Paket #"));
    Serial.print(alinanPaketSayisi);
    Serial.print(F(" | Frekans: 2446 MHz | Cozulen Icerik: \""));
    Serial.print((char*)cozulmusMesaj);
    Serial.print(F("\""));

    // Donanımsal sinyal gücü seviyesi kontrolü (-64 dBm üstü mü?)
    if (radio.testRPD()) {
      Serial.print(F(" [Guclu Sinyal]"));
    }
    Serial.println();

    delay(20);                  // LED'in parlaklığının görülmesi için kısa bekleme
    digitalWrite(LED_PIN, LOW); // LED'i söndür
  }
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - NIST standardı 128-bit simetrik anahtar ("kho-128-bit-key!") kullanarak
 *      gömülü sistemler (Arduino Mega/Uno) arasında gerçek zamanlı, gecikmesiz
 *      kriptolu veri alımını ve 32 baytlık tam paketlerin çift blok (2x16 bayt)
 *      halinde hatasız deşifre edilmesini hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Kablosuz ödeme terminalleri, endüstriyel PLC/SCADA kablosuz veri hatları,
 *    - Kurumsal bina otomasyonu, kablosuz güvenlik kameraları kontrol komutları
 *      ve ticari drone telemetri/kumanda bağlantılarında veri gizliliği (Data
 *      Confidentiality) sağlamak için kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - Taktik Saha Komuta-Kontrol (C2) Kısa Mesajlaşma: Askeri birlikler veya
 *      insansız kara araçları (İKA) arasında gönderilen kısa durum, koordinat
 *      veya görev komutlarının düşman unsurlarca okunmasını engellemede,
 *    - Mikro-İHA (FPV / Döner Kanat) Görev Yükü Tetikleme: İşlemci kapasitesi ve
 *      pil gücü kısıtlı mini/mikro İHA'larda AES-128'in düşük hesaplama maliyeti
 *      sayesinde uçuş kontrolcüsünü yormadan kriptolu komut alımında kullanılır.
 * ============================================================================ */
