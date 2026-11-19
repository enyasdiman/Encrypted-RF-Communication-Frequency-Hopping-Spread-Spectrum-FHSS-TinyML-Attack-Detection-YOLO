// ============================================================================
// BAŞLIK    : AES-256 Kriptolu 50 Kanallı Yüksek Hızlı FHSS Alıcı (10 Hop/Sn)
// DOSYA ADI : 09_AES256_FHSS_50Kanalli_YuksekHizli_10HopSn_Alici.ino
// AÇIKLAMA  : 50 kanallı frekans havuzunda saniyede 10 kez (100 ms slot süresi,
//             15 ms erken geçiş avansı) frekans atlayarak AES-256 şifreli paketleri
//             yakalar ve her 10 saniyede (100 pakette) bir Başarım Oranı raporlar.
// ============================================================================

#include <SPI.h>       // SPI haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım tanımlamaları
#include <RF24.h>      // RF24 radyo kontrol kütüphanesi
#include <Crypto.h>    // Kriptografi temel sınıfı
#include <AES.h>       // AES-256 şifreleme kütüphanesi

#define CE_PIN  9              // RF24 Chip Enable pini
#define CSN_PIN 53             // RF24 SPI Chip Select pini (Mega: 53)
#define LED_PIN LED_BUILTIN    // Paket alım göstergesi dahili LED

RF24 radio(CE_PIN, CSN_PIN);   // Radyo nesnesi
AES256 aes256;                 // AES-256 şifre çözme nesnesi

// 256-bit (32 Bayt) Ortak Gizli Anahtar ("kho-256-bit-secure-key-2026-ecb!")
const byte aesKey[32] = {
  'k', 'h', 'o', '-', '2', '5', '6', '-', 'b', 'i', 't', '-', 's', 'e', 'c', 'u',
  'r', 'e', '-', 'k', 'e', 'y', '-', '2', '0', '2', '6', '-', 'e', 'c', 'b', '!'
};

const byte address[6] = "00001"; // Haberleşme boru adresi

// Verici ile BİREBİR AYNI 50 Kanallı Ortak Frekans Havuzu
const uint8_t hopChannels[50] = {
  46, 12, 78, 25, 92, 5, 60, 33, 105, 18,
  85, 40, 115, 2, 70, 52, 98, 28, 65, 120,
  10, 82, 37, 110, 15, 75, 48, 95, 8, 62,
  22, 102, 43, 118, 30, 88, 55, 4, 68, 14,
  80, 35, 112, 20, 73, 50, 90, 6, 58, 124
};
const uint8_t TOPLAM_KANAL = 50;

// SANİYEDE 10 HOP -> 100 ms Slot Süresi
const unsigned long SLOT_SURESI = 100;

// Alıcı vericiden 15 ms ÖNCE kanala geçer ve pusuya yatar
const unsigned long ERKEN_GECIS_AVANSI = 15;

unsigned long sonSlotZamani = 0;   // Son kanal değişim zamanı (ms)
uint8_t hopIndex = 0;              // Aktif kanal indeksi (0..49)
uint16_t sonAlinanSlotNo = 0xFFFF; // Mükerrer paket okuma filtresi

enum Durum { SENKRON_ARANIYOR, SENKRON_KILITLI };
Durum sistemDurumu = SENKRON_ARANIYOR;

uint8_t kacirilanSlotSayisi = 0;   // Kaçırılan ardışık slot sayacı

// İstatistik ve Paket Başarım Oranı (PDR) Değişkenleri
uint16_t pencereBaslangicSlot = 0; // 100'lük pencerenin ilk slot numarası
uint16_t pencereAlinanPaket = 0;   // Pencerede alınan başarılı paket sayısı
uint32_t genelToplamAlinan = 0;    // Toplam alınan paket sayacı

// 32 Baytlık Kriptolu Senkron Paket Yapısı
struct __attribute__((packed)) SenkronPaket {
  char metin[24];          // 24 bayt mesaj içeriği ("kho-merhaba" imzalı)
  uint8_t hopIndeksi;      // 1 bayt aktif kanal indeksi
  uint8_t kanal;           // 1 bayt aktif RF kanalı
  uint16_t slotNo;         // 2 bayt slot sıra numarası
  uint32_t vericiMillis;   // 4 bayt verici milisaniye zamanı
};

// Donanımsal PLL kilidi sağlayan hızlı kanal değiştirme fonksiyonu
void kanalDegistir(uint8_t yeniKanal) {
  radio.stopListening();
  radio.setChannel(yeniKanal);
  delayMicroseconds(130); // PLL oturma süresi (130 us)
  radio.startListening();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  while (!Serial) { ; }

  Serial.println(F("\n=================================================="));
  Serial.println(F(" AES-256 50 KANAL YÜKSEK HIZLI RX (10 HOP/SN) "));
  Serial.println(F("=================================================="));

  if (!radio.begin()) {
    Serial.println(F("[HATA] nRF24L01 modulu algilanamadi! Kablolari kontrol edin."));
    while (1);
  }

  radio.setAutoAck(false);
  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_LOW);
  radio.setPayloadSize(32);
  radio.openReadingPipe(1, address);

  // Başlangıçta 2446 MHz (Kanal 46) baz frekansında bekle
  kanalDegistir(hopChannels[0]);

  // AES-256 gizli anahtarını yükle
  aes256.setKey(aesKey, sizeof(aesKey));

  Serial.println(F("[OK] Alıcı Dinlemede (10 Hop/sn Modu)."));
  Serial.println(F("[ARAMA] Baz kanalda (2446 MHz) verici ritmi araniyor...\n"));
}

void loop() {
  unsigned long suan = millis();

  // 1. YÜKSEK HIZLI SENKRON ATLAMA (Vericiden 15 ms erken atlar)
  if (sistemDurumu == SENKRON_KILITLI) {
    if (suan - sonSlotZamani >= SLOT_SURESI) {
      sonSlotZamani += SLOT_SURESI;
      hopIndex = (hopIndex + 1) % TOPLAM_KANAL;

      // Yeni frekansa geçip pusuya yat
      kanalDegistir(hopChannels[hopIndex]);
      kacirilanSlotSayisi++;

      // 30 slot (3 saniye) boyunca hiç sinyal alınamazsa baz frekansa dön:
      if (kacirilanSlotSayisi >= 30) {
        Serial.println(F("\n>>> [SENKRON KOPTU ❌] Baz kanala (2446 MHz) dönülüyor..."));
        sistemDurumu = SENKRON_ARANIYOR;
        hopIndex = 0;
        kacirilanSlotSayisi = 0;
        kanalDegistir(hopChannels[0]);
      }
    }
  }

  // 2. PAKET ALMA, AES-256 DEŞİFRE VE PERFORMANS ANALİZİ
  if (radio.available()) {
    unsigned long paketYakalamaAni = millis();

    byte gelenSifreli[32] = {0};
    SenkronPaket paket;

    // 32 baytlık şifreli paketi oku
    radio.read(&gelenSifreli, 32);

    // 2 blok halinde AES-256 deşifre et
    aes256.decryptBlock((byte*)&paket, gelenSifreli);
    aes256.decryptBlock(((byte*)&paket) + 16, gelenSifreli + 16);

    // İmza doğrulaması ("kho-merhaba")
    if (strncmp(paket.metin, "kho-merhaba", 11) == 0) {
      digitalWrite(LED_PIN, HIGH);

      // İlk yakalama ve senkron kilitleme
      if (sistemDurumu == SENKRON_ARANIYOR) {
        Serial.println(F("\n>>> [SENKRON KİLİTLENDİ ✔] 10 Hop/sn döngüsüne girildi!\n"));
        sistemDurumu = SENKRON_KILITLI;
        pencereBaslangicSlot = paket.slotNo;
        pencereAlinanPaket = 0;
      }

      // SAAT HİZALAMA: 15 ms erken geçiş avansı (Underflow korumalı)
      if (paketYakalamaAni >= ERKEN_GECIS_AVANSI) {
        sonSlotZamani = paketYakalamaAni - ERKEN_GECIS_AVANSI;
      } else {
        sonSlotZamani = 0;
      }

      hopIndex = paket.hopIndeksi;
      kacirilanSlotSayisi = 0;

      // Her slottaki paketi 1 kez istatistiğe dahil et
      if (paket.slotNo != sonAlinanSlotNo) {
        sonAlinanSlotNo = paket.slotNo;
        pencereAlinanPaket++;
        genelToplamAlinan++;

        char ekranaBas[25] = {0};
        memcpy(ekranaBas, paket.metin, 24);

        Serial.print(F("[RX #"));
        Serial.print(paket.slotNo);
        Serial.print(F("] "));
        Serial.print(2400 + hopChannels[hopIndex]);
        Serial.print(F(" MHz | Hop ["));
        if (paket.hopIndeksi < 10) Serial.print("0");
        Serial.print(paket.hopIndeksi);
        Serial.print(F("/49] | \""));
        Serial.print(ekranaBas);
        Serial.println(F("\""));

        // --- 100 PAKETLİK BAŞARI ORANI RAPORU (HER 10 SANİYEDE BİR) ---
        if ((uint16_t)(paket.slotNo - pencereBaslangicSlot) >= 100) {
          uint16_t toplamBeklenen = paket.slotNo - pencereBaslangicSlot;
          uint16_t kayipPaket = (toplamBeklenen >= pencereAlinanPaket) ? (toplamBeklenen - pencereAlinanPaket) : 0;
          float basariOrani = ((float)pencereAlinanPaket / (float)toplamBeklenen) * 100.0;
          float kayipOrani = 100.0 - basariOrani;

          Serial.println(F("\n=================================================="));
          Serial.println(F(" 📊 100 PAKETLİK PERFORMANS RAPORU (10 SN) "));
          Serial.println(F("=================================================="));
          Serial.print(F(" Gözlenen Slot Aralığı : #"));
          Serial.print(pencereBaslangicSlot);
          Serial.print(F(" - #"));
          Serial.print(paket.slotNo);
          Serial.print(F(" ("));
          Serial.print(toplamBeklenen);
          Serial.println(F(" Paket)"));

          Serial.print(F(" Başarıyla Alınan : "));
          Serial.print(pencereAlinanPaket);
          Serial.println(F(" Paket"));

          Serial.print(F(" Kayıp / Düşen Paket : "));
          Serial.print(kayipPaket);
          Serial.println(F(" Paket"));

          Serial.print(F(" >>> PAKET BAŞARI ORANI : %"));
          Serial.println(basariOrani, 2);

          Serial.print(F(" >>> PAKET KAYIP ORANI : %"));
          Serial.println(kayipOrani, 2);
          Serial.println(F("==================================================\n"));

          pencereBaslangicSlot = paket.slotNo;
          pencereAlinanPaket = 0;
        }
      }

      digitalWrite(LED_PIN, LOW);
    }
  }
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - 50 kanallı geniş frekans havuzunda kanalda kalma süresini (Dwell Time)
 *      100 milisaniyeye düşürerek **saniyede 10 frekans atlaması (10 Hop/sn)**
 *      gerçekleştirmeyi,
 *    - 15 ms hassas erken geçiş avansı ile yüksek hızda hem AES-256 şifre çözme
 *      hem de PLL kilitleme işlemlerini senkron kaybı olmadan yürütmeyi ve her
 *      10 saniyede bir (100 pakette) gerçek zamanlı PDR raporu sunmayı hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Düşük gecikmeli (low-latency) gerçek zamanlı robotik kontrol, otonom araç
 *      sürü (swarm) haberleşmesi ve hızlı hareket eden platformların Doppler/Çoklu
 *      Yol Sönümlemesi (Multipath Fading) etkilerinden kurtulması için kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - Takipçi Karıştırıcılara (Follower / Reactive Jammer) Karşı EPM (Elektronik
 *      Koruyucu Tedbirler): Gelişmiş düşman karıştırıcıları havadaki sinyali tespit
 *      edip o frekansa enerji yönlendirmeye çalışır (Follower Jamming). Kanalda
 *      kalma süresi 100 ms gibi çok kısa bir süreye indirildiğinde, düşman sistemi
 *      frekansı analiz edip karıştırma sinyali basana kadar dost sistem çoktan
 *      bir sonraki frekansa (50 kanallı listedeki yeni noktaya) zıplamış olur.
 *    - Kamikaze İHA / Dolanan Mühimmat (Loitering Munition) Terminal Safha Kontrolü:
 *      Hedefe dalış yapan bir İHA'nın son saniyelerinde yoğun yakın mesafe EH
 *      karıştırmasına maruz kaldığı terminal safhada, saniyede 10 kez kanal
 *      değiştirerek operatörle olan komuta bağını koparmadan hedefe kilitlenmesini sağlar.
 * ============================================================================ */
