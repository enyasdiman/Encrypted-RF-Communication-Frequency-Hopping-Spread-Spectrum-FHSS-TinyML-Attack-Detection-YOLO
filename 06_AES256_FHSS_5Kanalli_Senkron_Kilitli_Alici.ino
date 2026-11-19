// ============================================================================
// BAŞLIK    : AES-256 Kriptolu 5 Kanallı Senkron Frekans Atlamalı (Slot-FHSS) Alıcı
// DOSYA ADI : 06_AES256_FHSS_5Kanalli_Senkron_Kilitli_Alici.ino
// AÇIKLAMA  : Ortak 5 kanallı frekans listesi {46, 15, 75, 30, 95} üzerinde
//             400 ms slot süresi ve 25 ms erken zıplama (guard time) payı ile
//             AES-256 kriptolu frekans atlama (FHSS) senkronizasyonu yapar.
// ============================================================================

#include <SPI.h>       // SPI haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım tanımlamaları
#include <RF24.h>      // RF24 radyo kontrol kütüphanesi
#include <Crypto.h>    // Kriptografi temel kütüphanesi
#include <AES.h>       // AES-256 blok şifreleme kütüphanesi

#define CE_PIN  9              // RF24 Chip Enable pini
#define CSN_PIN 53             // RF24 SPI Chip Select pini (Mega: 53)
#define LED_PIN LED_BUILTIN    // Paket alım göstergesi LED pini

RF24 radio(CE_PIN, CSN_PIN);   // RF24 radyo nesnesi
AES256 aes256;                 // AES-256 şifre çözme nesnesi

// 256-bit (32 Bayt) Ortak Gizli Anahtar
const byte aesKey[32] = {
  'k', 'h', 'o', '-', '2', '5', '6', '-', 'b', 'i', 't', '-', 's', 'e', 'c', 'u',
  'r', 'e', '-', 'k', 'e', 'y', '-', '2', '0', '2', '6', '-', 'e', 'c', 'b', '!'
};

const byte address[6] = "00001"; // Ortak haberleşme boru adresi

// Verici ile BİREBİR AYNI 5 Kanallı Ortak Frekans Atlama Dizisi
// Frekanslar: 2446 MHz, 2415 MHz, 2475 MHz, 2430 MHz, 2495 MHz
const uint8_t hopChannels[] = { 46, 15, 75, 30, 95 };
const uint8_t TOPLAM_KANAL = sizeof(hopChannels) / sizeof(hopChannels[0]);

const unsigned long SLOT_SURESI = 400;        // Vericinin her kanalda kalma (slot) süresi (400 ms)
const unsigned long ERKEN_ZIPLAMA_PAYI = 25;  // Alıcının vericiden 25 ms ÖNCE yeni kanala geçip bekleme payı

unsigned long sonSlotZamani = 0;  // Son kanal geçişinin referans zamanı (ms)
uint8_t hopIndex = 0;             // Kanal listesindeki aktif indeks (0..4)
bool buSlottaPaketAlindi = false; // Mevcut slotta paket yakalanıp yakalanmadığı bayrağı

// Alıcı Senkronizasyon Durum Makinesi (State Machine)
enum Durum { SENKRON_ARANIYOR, SENKRON_KILITLI };
Durum sistemDurumu = SENKRON_ARANIYOR;

uint8_t kacirilanSlotSayisi = 0;  // Üst üste sinyal alınamayan slot sayacı (watchdog)

// 32 Baytlık Kriptolu Senkronizasyon Paket Yapısı (Struct)
struct __attribute__((packed)) SenkronPaket {
  char metin[24];          // 24 bayt: Mesaj yükü ve doğrulama imzası
  uint8_t hopIndeksi;      // 1 bayt : Vericinin o anki kanal dizi indeksi
  uint8_t kanal;           // 1 bayt : Vericinin o anki RF kanal numarası
  uint16_t slotNo;         // 2 bayt : Gönderilen toplam slot/paket sıra numarası
  uint32_t vericiMillis;   // 4 bayt : Vericinin çalışma zamanı (ms) -> Toplam = 32 Bayt
};

// ----------------------------------------------------------------------------
// FONKSİYON : guvenliKanalDegistir
// GÖREVİ    : nRF24L01'de PLL (Phase-Locked Loop) frekans sentezleyicisinin
//             yeni kanala sağlıklı kilitlenmesi için dinlemeyi durdurup kanalı
//             değiştirir ve tekrar dinlemeye başlar.
// ----------------------------------------------------------------------------
void guvenliKanalDegistir(uint8_t yeniKanal) {
  radio.stopListening();       // RX modundan çık (Standby-I)
  radio.setChannel(yeniKanal); // Yeni RF kanalını yazmaca yükle
  radio.startListening();      // RX moduna geri dön (PLL yeni frekansa kilitlenir)
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  while (!Serial) { ; }

  Serial.println(F("\n=================================================="));
  Serial.println(F("   AES-256 SENKRON KILITLI (SLOT-FHSS) ALICI      "));
  Serial.println(F("=================================================="));

  if (!radio.begin()) {
    Serial.println(F("[HATA] nRF24L01 modulu algilanamadi! Kablolari kontrol edin."));
    while (1);
  }

  radio.setAutoAck(false);           // FHSS'de hızlı kanal geçişi için Auto-ACK kapalı
  radio.setDataRate(RF24_1MBPS);     // 1 Mbps veri hızı
  radio.setPALevel(RF24_PA_LOW);     // Düşük PA gücü
  radio.setPayloadSize(32);          // 32 bayt sabit paket boyutu
  radio.openReadingPipe(1, address); // Okuma borusunu aç

  // Başlangıçta Baz Kanalda (Kanal 46 -> 2446 MHz) pusuda bekle
  guvenliKanalDegistir(hopChannels[0]);

  // AES-256 gizli anahtarını yükle
  aes256.setKey(aesKey, sizeof(aesKey));

  Serial.println(F("[OK] Alıcı Hazır."));
  Serial.println(F("[ARAMA] Baz kanalda (2446 MHz) vericinin saat ritmi araniyor...\n"));
}

void loop() {
  unsigned long suan = millis();

  // -----------------------------------------------------------
  // 1. SENKRON ATLAMA MOTORU (Vericiden 25 ms Hemen Önce Zıplama)
  // -----------------------------------------------------------
  if (sistemDurumu == SENKRON_KILITLI) {
    // Slot süresi (400 ms) dolduysa bir sonraki kanala atla
    if (suan - sonSlotZamani >= SLOT_SURESI) {
      sonSlotZamani += SLOT_SURESI; // Slot saatini 400 ms ilerlet (kaymayı önler)
      hopIndex = (hopIndex + 1) % TOPLAM_KANAL; // Kanal listesinde dairesel ilerle

      // Güvenli şekilde yeni frekansa geç
      guvenliKanalDegistir(hopChannels[hopIndex]);

      // Bu slotta hiç paket alınamadıysa kaçırılan slot sayacını artır
      if (!buSlottaPaketAlindi) {
        kacirilanSlotSayisi++;
      }
      buSlottaPaketAlindi = false; // Yeni slot için bayrağı sıfırla

      // Eğer 10 slot (4 saniye) boyunca hiçbir frekansta sinyal alınamazsa senkron kopmuştur:
      if (kacirilanSlotSayisi >= 10) {
        Serial.println(F("\n>>> [SENKRON KOPTU ❌] Sinyal ritmi kayboldu! Baz kanala (2446 MHz) dönülüyor..."));
        sistemDurumu = SENKRON_ARANIYOR;
        hopIndex = 0;
        kacirilanSlotSayisi = 0;
        guvenliKanalDegistir(hopChannels[0]); // Baz kanala (2446 MHz) geri dön ve bekle
      }
    }
  }

  // -----------------------------------------------------------
  // 2. PAKET DİNLEME, AES-256 DEŞİFRE VE SAAT HİZALAMA
  // -----------------------------------------------------------
  if (radio.available()) {
    unsigned long paketGelisAni = millis(); // Seri port gecikmesinden etkilenmemek için geliş anını hemen kaydet

    byte gelenSifreli[32] = {0};
    SenkronPaket paket;

    // 32 baytlık şifreli paketi oku
    radio.read(&gelenSifreli, 32);

    // AES-256 ile iki adet 16 baytlık bloğu deşifre et
    aes256.decryptBlock((byte*)&paket, gelenSifreli);
    aes256.decryptBlock(((byte*)&paket) + 16, gelenSifreli + 16);

    // Kriptografik doğrulama: Çözülen metin beklenen imza ile başlıyor mu?
    if (strncmp(paket.metin, "sponsored", 9) == 0) {
      digitalWrite(LED_PIN, HIGH);

      // İLK YAKALAMA ANINDA SENKRONİZE OL:
      if (sistemDurumu == SENKRON_ARANIYOR) {
        Serial.println(F("\n>>> [SENKRON KİLİTLENDİ ✔] Vericinin saat ritmi yakalandı!"));
        Serial.println(F(">>> Artık vericiyle birlikte aynı anda frekans atlanıyor...\n"));
        sistemDurumu = SENKRON_KILITLI;
      }

      // SAAT HİZALAMA (KRİTİK NOKTA):
      // Verici bir sonraki slota tam 400 ms sonra geçecek.
      // Alıcının bir sonraki kanala vericiden 25 ms ÖNCE geçip orada hazır beklemesi için
      // referans zamanımızı (paketGelisAni - ERKEN_ZIPLAMA_PAYI) olarak kuruyoruz:
      sonSlotZamani = paketGelisAni - ERKEN_ZIPLAMA_PAYI;
      hopIndex = paket.hopIndeksi; // Vericinin bildirdiği kanal indeksine hizalan
      kacirilanSlotSayisi = 0;     // Kayıp sayacını sıfırla

      // Aynı slotta birden fazla paket gelirse ekrana sadece 1 kez yazdır
      if (!buSlottaPaketAlindi) {
        buSlottaPaketAlindi = true;

        char ekranaBas[25] = {0};
        memcpy(ekranaBas, paket.metin, 24);

        Serial.print(F("[RX-SLOT #"));
        Serial.print(paket.slotNo);
        Serial.print(F("] "));
        Serial.print(2400 + hopChannels[hopIndex]);
        Serial.print(F(" MHz (Kanal "));
        Serial.print(hopChannels[hopIndex]);
        Serial.print(F(") | Hop: "));
        Serial.print(paket.hopIndeksi);
        Serial.print(F(" | \""));
        Serial.print(ekranaBas);
        Serial.println(F("\""));
      }

      digitalWrite(LED_PIN, LOW);
    }
  }
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - 2.4 GHz bandının farklı uçlarına dağıtılmış 5 ortak kanal {46, 15, 75, 30, 95}
 *      üzerinde Zaman Bölmeli Frekans Atlama (Time-Slotted FHSS) gerçekleştirmeyi,
 *    - 25 ms erken zıplama payı (Guard Time) ile mikrodenetleyici kristal saat
 *      kaymalarını (clock drift) telafi etmeyi,
 *    - Sinyal koptuğunda (10 ardışık kayıp slot) otomatik olarak Baz Kanala (2446 MHz)
 *      dönerek kendi kendine yeniden senkronize olmayı (Self-Healing Synchronization)
 *      hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Yoğun Wi-Fi ve Bluetooth trafiğinin bulunduğu fabrika, hastane ve ofis
 *      ortamlarında sabit bir frekanstaki parazitlerden (co-channel interference)
 *      kaçınarak yüksek güvenilirlikli kablosuz veri aktarımı sağlamak için kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - Nokta Karıştırmaya (Spot Jamming) Karşı Anti-Jamming Telsiz/Veri Linki:
 *      Düşman Elektronik Harp (EH / EW) sistemleri sabit bir frekansı (ör. 2446 MHz)
 *      tespit edip karıştırdığında (jamming), sistem 400 ms içinde diğer 4 temiz
 *      kanala (2415, 2475, 2430, 2495 MHz) atlayarak iletişimin %80 oranında
 *      kesintisiz devam etmesini sağlar.
 *    - Otomatik Rendezvous (Buluşma) Mekanizması: Ağır elektronik taarruz altında
 *      senkronizasyon kopsa bile alıcının baz kanala dönüp ilk şifreli pakette
 *      tekrar kilitlenmesi, askeri İHA'ların kontrol istasyonuyla bağlantıyı
 *      otomatik kurtarmasında (Link Recovery) kritik rol oynar.
 * ============================================================================ */
