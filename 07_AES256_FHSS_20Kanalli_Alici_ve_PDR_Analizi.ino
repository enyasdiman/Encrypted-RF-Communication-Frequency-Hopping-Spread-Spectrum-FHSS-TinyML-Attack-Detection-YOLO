// ============================================================================
// BAŞLIK    : AES-256 Kriptolu 20 Kanallı FHSS Alıcı ve Paket Başarım (PDR) Analizörü
// DOSYA ADI : 07_AES256_FHSS_20Kanalli_Alici_ve_PDR_Analizi.ino
// AÇIKLAMA  : 20 kanallı ortak frekans havuzu üzerinde 350 ms slot süresiyle
//             AES-256 kriptolu FHSS iletişimi gerçekleştirir. Her 100 pakette bir
//             Paket Başarım Oranı (PDR/PSR) ve Paket Kayıp Oranı raporu üretir.
// ============================================================================

#include <SPI.h>       // SPI haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım tanımlamaları
#include <RF24.h>      // RF24 radyo kontrol kütüphanesi
#include <Crypto.h>    // Kriptografi temel sınıfı
#include <AES.h>       // AES-256 blok şifreleme kütüphanesi

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

// Verici ile BİREBİR AYNI 20 Kanallı Ortak Frekans Atlama Havuzu (ISM 2.4 GHz Bandı)
const uint8_t hopChannels[20] = {
  46, 12, 78, 25, 92, 5, 60, 33, 105, 18,
  85, 40, 115, 2, 70, 52, 98, 28, 65, 120
};
const uint8_t TOPLAM_KANAL = 20;

const unsigned long SLOT_SURESI = 350;        // Her kanalda bekleme süresi: 350 ms
// KRİTİK AYAR: Alıcı, vericiden 25 ms ÖNCE yeni kanala geçip pusuda bekler!
const unsigned long ERKEN_GECIS_AVANSI = 25;

unsigned long sonSlotZamani = 0;      // Son slot geçiş zamanı (ms)
uint8_t hopIndex = 0;                 // Aktif kanal indeksi (0..19)
uint16_t sonAlinanSlotNo = 0xFFFF;    // Aynı slotta mükerrer paket sayımını önleyen filtre

enum Durum { SENKRON_ARANIYOR, SENKRON_KILITLI };
Durum sistemDurumu = SENKRON_ARANIYOR;

uint8_t kacirilanSlotSayisi = 0;      // Sinyal alınamayan ardışık slot sayacı

// --- İSTATİSTİK VE PAKET BAŞARIM ORANI (PDR) DEĞİŞKENLERİ ---
uint16_t pencereBaslangicSlot = 0;    // 100'lük analiz penceresinin başladığı slot numarası
uint16_t pencereAlinanPaket = 0;      // Mevcut 100'lük pencerede başarıyla alınan paket sayısı
uint32_t genelToplamAlinan = 0;       // Cihaz açıldığından beri alınan toplam paket sayısı

// 32 Baytlık Kriptolu Senkron Paket Yapısı
struct __attribute__((packed)) SenkronPaket {
  char metin[24];          // 24 bayt mesaj içeriği
  uint8_t hopIndeksi;      // 1 bayt aktif kanal indeksi
  uint8_t kanal;           // 1 bayt aktif kanal numarası
  uint16_t slotNo;         // 2 bayt slot sıra numarası
  uint32_t vericiMillis;   // 4 bayt verici zaman damgası
};

// ----------------------------------------------------------------------------
// FONKSİYON : kanalDegistir
// GÖREVİ    : nRF24L01'in frekansı gerçekten değiştirebilmesi için RX modunu
//             kapatır, yeni kanalı ayarlar, PLL'in oturması için 130 us bekler
//             ve tekrar RX dinleme modunu açar.
// ----------------------------------------------------------------------------
void kanalDegistir(uint8_t yeniKanal) {
  radio.stopListening();
  radio.setChannel(yeniKanal);
  delayMicroseconds(130); // PLL frekans sentezleyicinin oturması için 130 mikrosaniye
  radio.startListening();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  while (!Serial) { ; }

  Serial.println(F("\n=================================================="));
  Serial.println(F("   AES-256 20 KANALLI SENKRON ALICI VE ANALIZ     "));
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

  // Başlangıçta 2446 MHz (Kanal 46 - Baz Kanal) frekansında bekle
  kanalDegistir(hopChannels[0]);

  // 256-bit AES anahtarını yükle
  aes256.setKey(aesKey, sizeof(aesKey));

  Serial.println(F("[OK] Alici Dinlemede."));
  Serial.println(F("[ARAMA] Baz kanalda (2446 MHz) vericinin saat ritmi araniyor...\n"));
}

void loop() {
  unsigned long suan = millis();

  // 1. SENKRON FREKANS ATLAMA MOTORU
  if (sistemDurumu == SENKRON_KILITLI) {
    if (suan - sonSlotZamani >= SLOT_SURESI) {
      sonSlotZamani += SLOT_SURESI;
      hopIndex = (hopIndex + 1) % TOPLAM_KANAL;

      // Yeni kanala güvenli geçiş yap
      kanalDegistir(hopChannels[hopIndex]);
      kacirilanSlotSayisi++;

      // 20 kanalda 25 slot boyunca (~8.75 sn) hiç sinyal yoksa senkron kopmuştur:
      if (kacirilanSlotSayisi >= 25) {
        Serial.println(F("\n>>> [SENKRON KOPTU ❌] Baz kanala (2446 MHz) dönülüyor..."));
        sistemDurumu = SENKRON_ARANIYOR;
        hopIndex = 0;
        kacirilanSlotSayisi = 0;
        kanalDegistir(hopChannels[0]);
      }
    }
  }

  // 2. PAKET ALMA, AES-256 DEŞİFRE VE PAKET BAŞARIM ORANI HESAPLAMA
  if (radio.available()) {
    // Paketin geldiği ilk milisaniyeyi hemen yakala (AES ve Serial gecikmesinden önce!)
    unsigned long paketYakalamaAni = millis();

    byte gelenSifreli[32] = {0};
    SenkronPaket paket;

    // 32 baytlık şifreli paketi oku
    radio.read(&gelenSifreli, 32);

    // İki blok (2x16 bayt) halinde AES-256 deşifre işlemi
    aes256.decryptBlock((byte*)&paket, gelenSifreli);
    aes256.decryptBlock(((byte*)&paket) + 16, gelenSifreli + 16);

    // Kriptografik doğrulama kontrolü
    if (strncmp(paket.metin, "sponsored", 9) == 0) {
      digitalWrite(LED_PIN, HIGH);

      // İlk kilitlenme anı
      if (sistemDurumu == SENKRON_ARANIYOR) {
        Serial.println(F("\n>>> [SENKRON KİLİTLENDİ ✔] 20 Kanallı frekans atlama döngüsüne girildi!\n"));
        sistemDurumu = SENKRON_KILITLI;
        pencereBaslangicSlot = paket.slotNo;
        pencereAlinanPaket = 0;
      }

      // KRİTİK SAAT HİZALAMA:
      // Verici paketi slotun başında gönderdi. Biz bir sonraki kanala vericiden
      // 25 ms ERKEN geçmek için slot başlangıç zamanımızı 25 ms geriye kuruyoruz:
      sonSlotZamani = paketYakalamaAni - ERKEN_GECIS_AVANSI;
      hopIndex = paket.hopIndeksi;
      kacirilanSlotSayisi = 0;

      // Eğer aynı slot içinde birden fazla tekrar paketi basılıyorsa istatistiği sadece 1 kez say
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
        Serial.print(F(" MHz (Kanal "));
        Serial.print(hopChannels[hopIndex]);
        Serial.print(F(") | Hop ["));
        if (paket.hopIndeksi < 10) Serial.print("0");
        Serial.print(paket.hopIndeksi);
        Serial.print(F("/19] | \""));
        Serial.print(ekranaBas);
        Serial.println(F("\""));

        // --- 100 PAKETLİK PAKET BAŞARIM ORANI (PDR) RAPORU ---
        if ((uint16_t)(paket.slotNo - pencereBaslangicSlot) >= 100) {
          uint16_t toplamBeklenen = paket.slotNo - pencereBaslangicSlot;
          uint16_t kayipPaket = (toplamBeklenen >= pencereAlinanPaket) ? (toplamBeklenen - pencereAlinanPaket) : 0;
          float basariOrani = ((float)pencereAlinanPaket / (float)toplamBeklenen) * 100.0;
          float kayipOrani = 100.0 - basariOrani;

          Serial.println(F("\n=================================================="));
          Serial.println(F(" 📊 100 PAKETLİK PERFORMANS VE BAŞARI RAPORU "));
          Serial.println(F("=================================================="));
          Serial.print(F(" Gözlenen Slot Aralığı : #"));
          Serial.print(pencereBaslangicSlot);
          Serial.print(F(" - #"));
          Serial.print(paket.slotNo);
          Serial.print(F(" ("));
          Serial.print(toplamBeklenen);
          Serial.println(F(" Paket)"));

          Serial.print(F(" Başarıyla Alınan      : "));
          Serial.print(pencereAlinanPaket);
          Serial.println(F(" Paket"));

          Serial.print(F(" Kayıp / Düşen Paket   : "));
          Serial.print(kayipPaket);
          Serial.println(F(" Paket"));

          Serial.print(F(" >>> PAKET BAŞARI ORANI : %"));
          Serial.println(basariOrani, 2);

          Serial.print(F(" >>> PAKET KAYIP ORANI  : %"));
          Serial.println(kayipOrani, 2);
          Serial.println(F("==================================================\n"));

          // Yeni 100'lük analiz penceresini başlat
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
 *    - 20 farklı frekans kanalından oluşan genişletilmiş sözde-rastgele (pseudo-random)
 *      kanal havuzunda 350 ms slot süresiyle AES-256 kriptolu FHSS iletişimi kurmayı,
 *    - Donanımsal PLL (Phase-Locked Loop) oturma süresi olan 130 mikrosaniyeyi
 *      gözeterek sıfır kanal geçiş hatası sağlamayı,
 *    - Her 100 slotluk pencerede gerçek zamanlı Paket Başarım Oranı (%PDR) ve
 *      Paket Kayıp Oranı (%) istatistiği çıkarmayı hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Profesyonel İHA/Drone kumanda protokolleri (ELRS, Crossfire, FASST benzeri),
 *      kablosuz acil durum durdurma (E-Stop) sistemleri ve spektrum kirliliği
 *      yüksek şehir içi kablosuz telemetri ağlarında bağlantı kalitesini (QoS -
 *      Quality of Service) sürekli izlemek için kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - Kısmi Bant Karıştırmaya (Partial-Band Jamming) Karşı Dayanıklılık Analizi:
 *      20 kanala yayılan atlama dizisi sayesinde, düşman karıştırıcı (Jammer) 2.4 GHz
 *      bandının belirli bir bölümünü tamamen köreltse bile, sistemin paket başarım
 *      oranının (%PDR) ne kadar etkilendiğini 100 paketlik pencerelerle nicel olarak
 *      ölçer.
 *    - Askeri Yazılım Tabanlı Telsiz (SDR / HAVE QUICK & SINCGARS) Simülasyonu:
 *      Modern taktik saha telsizlerinin temelini oluşturan kriptolu zaman senkronu
 *      (TOD - Time of Day) ve kanal kalitesi değerlendirme (LQA - Link Quality
 *      Analysis) mekanizmalarının gömülü prototipini temsil eder.
 * ============================================================================ */
