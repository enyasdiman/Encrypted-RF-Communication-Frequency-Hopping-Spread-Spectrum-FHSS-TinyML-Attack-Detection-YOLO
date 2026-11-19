// ============================================================================
// BAŞLIK    : AES-128 ECB Kriptolu RF Alıcı (Şifreli HEX ve Açık Metin Analizli)
// DOSYA ADI : 02_AES128_ECB_HexAnahtar_Sifreli_ve_Acik_Metin_Alici.ino
// AÇIKLAMA  : 2446 MHz frekansında 128-bit (16 Bayt) Hexadecimal gizli anahtar
//             kullanarak gelen 32 baytlık (2x16 bayt blok) şifreli RF paketlerini
//             hem şifreli (kriptosuz okunamaz HEX) haliyle hem de AES-128 ECB ile
//             çözülmüş açık metin haliyle ekrana basar ve RPD/RSSI ölçümü yapar.
// ============================================================================

#include <SPI.h>       // SPI donanım haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım tanımlamaları
#include <RF24.h>      // RF24 radyo kontrol kütüphanesi
#include <Crypto.h>    // Arduino kriptografi temel kütüphanesi
#include <AES.h>       // AES (128/192/256 bit) blok şifreleme kütüphanesi

// --- KULLANILAN ALICI KARTA GÖRE PİN SEÇİMİ ---
// Arduino Mega için CSN_PIN = 53 (Uno/Nano kullanılıyorsa CSN_PIN = 10 yapılmalıdır)
#define CE_PIN   9             // RF24 Chip Enable pini
#define CSN_PIN 53             // RF24 SPI Chip Select pini
#define LED_PIN LED_BUILTIN    // Paket alım göstergesi dahili LED

RF24 radio(CE_PIN, CSN_PIN);   // RF24 radyo nesnesi

// Verici ile BİREBİR AYNI 5 baytlık haberleşme boru (pipe) adresi
const byte address[6] = "00001";

// 128-bit (16 Bayt) AES Gizli Anahtarı (Verici ile birebir aynı olmalıdır)
// ASCII karşılığı: "KHO_GIZLI_KEY128"
const byte aesKey[16] = {
  0x4B, 0x48, 0x4F, 0x5F, 0x47, 0x49, 0x5A, 0x4C, // "KHO_GIZL" (İlk 8 bayt)
  0x49, 0x5F, 0x4B, 0x45, 0x59, 0x31, 0x32, 0x38  // "I_KEY128" (Son 8 bayt)
};

AES128 aes128;                        // 128-bit AES şifre çözme nesnesi
unsigned long alinanPaketSayisi = 0;  // Başarılı alınan paket sayacı

// ----------------------------------------------------------------------------
// FONKSİYON : cozECB
// GÖREVİ    : nRF24L01'den gelen 32 baytlık şifreli paketi, AES'in 16 baytlık
//             blok yapısına uygun olarak 2 parça (2x16 bayt) halinde ECB 
//             (Electronic Codebook) modunda deşifre eder.
// ----------------------------------------------------------------------------
void cozECB(byte* sifreliVeri, byte* cozulmusVeri, const byte* key) {
  aes128.setKey(key, 16);                                   // 16 baytlık (128-bit) anahtarı yükle
  aes128.decryptBlock(cozulmusVeri, sifreliVeri);           // 1. Blok: İlk 16 baytı (0..15) çöz
  aes128.decryptBlock(cozulmusVeri + 16, sifreliVeri + 16); // 2. Blok: İkinci 16 baytı (16..31) çöz
}

void setup() {
  pinMode(LED_PIN, OUTPUT);     // LED pinini çıkış yap
  digitalWrite(LED_PIN, LOW);   // Başlangıçta LED'i kapat

  Serial.begin(115200);         // 115200 baud hızında seri monitörü başlat
  while (!Serial) { ; }         // Seri port hazır olana kadar bekle

  Serial.println(F("\n========================================================"));
  Serial.println(F(" 2446 MHz 'kho-merhaba' AES-128 ECB ALICI (RX) TESTI    "));
  Serial.println(F("========================================================"));

  // 1. nRF24L01 Modülünü Başlat
  if (!radio.begin()) {
    Serial.println(F("[HATA ❌] Alici nRF24L01 modulu algilanamadi! Kablolari kontrol edin."));
    while (1); // Donanım bulunamazsa sistemi durdur
  }
  Serial.println(F("[OK ✔] nRF24L01 Modulu basariyla baslatildi."));

  // 2. Verici ile Eşleşen RF Kanal ve Modülasyon Ayarları
  radio.setChannel(46);           // Sabit Kanal 46 -> 2400 + 46 = 2446 MHz
  radio.setAutoAck(false);        // Otomatik ACK yanıtını devre dışı bırak
  radio.setDataRate(RF24_1MBPS);  // 1 Mbps veri aktarım hızı
  radio.setPALevel(RF24_PA_LOW);  // Düşük güç seviyesi (amfi doygunluğunu önler)

  // 3. Okuma Kanalını Aç ve Dinlemeye Başla
  radio.openReadingPipe(1, address); // Pipe 1 üzerinden "00001" adresini dinle
  radio.startListening();            // Alıcı (RX) modunu aktifleştir

  Serial.print(F("[BILGI] Dinleme Frekansi: "));
  Serial.print(2400 + radio.getChannel());
  Serial.println(F(" MHz | Kripto: AES-128 ECB"));
  Serial.println(F("[BILGI] Sifreli sinyal bekleniyor... Lutfen vericiyi calistirin.\n"));
}

void loop() {
  // Havada okunmaya hazır şifreli bir RF paketi var mı?
  if (radio.available()) {
    byte gelenSifreliMesaj[32]; // Havadan okunacak 32 baytlık şifreli (ciphertext) tamponu
    byte cozulmusMesaj[33];     // Çözülen metin + sonuna güvenli '\0' (null-terminator) için 33 bayt
    memset(cozulmusMesaj, 0, sizeof(cozulmusMesaj)); // Tamponu sıfırla

    // 1. 32 baytlık şifreli veriyi nRF24L01'den oku
    radio.read(&gelenSifreliMesaj, sizeof(gelenSifreliMesaj));

    // 2. AES-128 ECB fonksiyonu ile 2 bloğu şifreden çöz
    cozECB(gelenSifreliMesaj, cozulmusMesaj, aesKey);
    cozulmusMesaj[32] = '\0'; // String taşmasını (buffer overflow) önlemek için sonlandırıcı ekle

    alinanPaketSayisi++;        // Paket sayacını artır
    digitalWrite(LED_PIN, HIGH); // Paket alındığında LED'i yak

    // 3. Seri Port Ekranına hem şifreli HEX özetini hem de çözülen açık metni yazdır
    Serial.print(F("[ALINDI ✔] Paket #"));
    Serial.print(alinanPaketSayisi);
    Serial.print(F(" | Sifreli (HEX): "));
    for (int i = 0; i < 6; i++) { // Havadaki şifreli verinin ilk 6 baytını HEX olarak göster
      if (gelenSifreliMesaj[i] < 0x10) Serial.print('0');
      Serial.print(gelenSifreliMesaj[i], HEX);
      Serial.print(' ');
    }
    Serial.print(F(".. | Cozulen: \""));
    Serial.print((char*)cozulmusMesaj); // Deşifre edilmiş orijinal metni yazdır
    Serial.print(F("\""));

    // 4. Sinyal Gücü (RSSI / RPD - Received Power Detector) Kontrolü (-64 dBm eşiği)
    if (radio.testRPD()) {
      Serial.print(F(" [Guclu Sinyal]"));
    }
    Serial.println();

    delay(20);                  // Görsel LED gecikmesi
    digitalWrite(LED_PIN, LOW); // LED'i söndür
  }
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - Kablosuz kanaldan geçen 32 baytlık verinin 128-bit (16 bayt) Hexadecimal
 *      gizli anahtar (`KHO_GIZLI_KEY128`) ile AES-128 ECB modunda şifrelendiğinde
 *      dışarıdan dinleyen (kriptosuz/anahtarsız) bir alıcıya nasıl anlamsız HEX
 *      baytlar olarak göründüğünü, doğru anahtara sahip alıcıda ise nasıl 2 blok
 *      halinde çözülüp açık metne dönüştüğünü kanıtlamayı hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Akıllı ev kilitleri, garaj/bariyer kumandaları, IoT sensör düğümleri ve
 *      kablosuz medikal cihazlarda (implant/hasta takip) verinin üçüncü kişilerce
 *      okunmasını önlemek ve düşük işlemci gücüyle (8-bit AVR mikrodenetleyicilerde)
 *      hızlı şifreleme sağlamak için kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - COMSEC (İletişim Güvenliği) Eğitim ve Doğrulama Test Yatakları: Pasif
 *      dinleme (Eavesdropping / Sniffing) yapan düşman elektronik destek (ED)
 *      unsurlarının havadaki paketi yakalasa bile yalnızca anlamsız HEX verisi
 *      elde edeceğini gösteren kriptografik doğrulama senaryolarında,
 *    - Düşük Güçlü Taktik Sensör Ağları (UGS - Unattended Ground Sensors): Sınır
 *      hattına veya kritik geçiş bölgelerine bırakılan pil beslemeli sismik,
 *      akustik veya hareket sensörlerinin merkeze gönderdiği alarm paketlerinin
 *      düşük enerji tüketimiyle şifrelenmesinde kullanılır.
 * ============================================================================ */
