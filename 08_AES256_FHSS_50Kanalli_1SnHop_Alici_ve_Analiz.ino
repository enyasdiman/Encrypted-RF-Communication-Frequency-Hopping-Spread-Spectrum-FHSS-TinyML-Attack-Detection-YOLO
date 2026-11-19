// ============================================================================
// BAŞLIK    : AES-256 Kriptolu 50 Kanallı Geniş Spektrum FHSS Alıcı (1 Hop/Sn)
// DOSYA ADI : 08_AES256_FHSS_50Kanalli_1SnHop_Alici_ve_Analiz.ino
// AÇIKLAMA  : 50 kanallı geniş frekans havuzu üzerinde her saniyede 1 kanal 
//             atlayarak (1000 ms slot süresi, 50 ms erken geçiş avansı) AES-256
//             kriptolu haberleşme yapar ve her 100 pakette bir Başarım Oranı basar.
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

// Verici ile BİREBİR AYNI 50 Kanallı Ortak Frekans Havuzu (2402 MHz - 2524 MHz arası)
const uint8_t hopChannels[50] = {
  46, 12, 78, 25, 92, 5, 60, 33, 105, 18,
  85, 40, 115, 2, 70, 52, 98, 28, 65, 120,
  10, 82, 37, 110, 15, 75, 48, 95, 8, 62,
  22, 102, 43, 118, 30, 88, 55, 4, 68, 14,
  80, 35, 112, 20, 73, 50, 90, 6, 58, 124
};
const uint8_t TOPLAM_KANAL = 50;

// HER SANİYE 1 KANAL ATLAMA: 1000 ms Slot Süresi
const unsigned long SLOT_SURESI = 1000;

// Alıcı, vericiden 50 ms ÖNCE yeni kanala geçip pusuya yatar
const unsigned long ERKEN_GECIS_AVANSI = 50;

unsigned long sonSlotZamani = 0;   // Son slot geçiş zamanı (ms)
uint8_t hopIndex = 0;              // 50 kanallı dizideki aktif indeks (0..49)
uint16_t sonAlinanSlotNo = 0xFFFF; // Aynı slotta mükerrer okuma filtresi

enum Durum { SENKRON_ARANIYOR, SENKRON_KILITLI };
Durum sistemDurumu = SENKRON_ARANIYOR;

uint8_t kacirilanSlotSayisi = 0;   // Üst üste sinyal alınamayan slot sayacı

// İstatistik ve Paket Başarım Oranı (PDR) Değişkenleri
uint16_t pencereBaslangicSlot = 0; // Analiz penceresi başlangıç slotu
uint16_t pencereAlinanPaket = 0;   // Pencerede başarılı alınan paket sayısı
uint32_t genelToplamAlinan = 0;    // Toplam alınan paket sayacı

// 32 Baytlık Kriptolu Senkron Paket Yapısı
struct __attribute__((packed)) SenkronPaket {
  char metin[24];          // 24 bayt mesaj ("kho-merhaba" doğrulama imzalı)
  uint8_t hopIndeksi;      // 1 bayt aktif kanal indeksi
  uint8_t kanal;           // 1 bayt aktif RF kanalı
  uint16_t slotNo;         // 2 bayt slot numarası
  uint32_t vericiMillis;   // 4 bayt verici milisaniye sayacı
};

// Donanımsal PLL kilidi sağlayan güvenli kanal değiştirme fonksiyonu
void kanalDegistir(uint8_t yeniKanal) {
  radio.stopListening();
  radio.setChannel(yeniKanal);
  delayMicroseconds(150); // PLL frekans oturma süresi (150 mikrosaniye)
  radio.startListening();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  while (!Serial) { ; }

  Serial.println(F("\n=================================================="));
  Serial.println(F(" AES-256 50 KANALLI ALICI (1 SN / HOP & ANALIZ) "));
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

  // Başlangıçta 2446 MHz (Kanal 46) baz frekansında pusuya yat
  kanalDegistir(hopChannels[0]);

  // AES-256 anahtarını yükle
  aes256.setKey(aesKey, sizeof(aesKey));

  Serial.println(F("[OK] 50 Kanallı Alıcı Hazır."));
  Serial.println(F("[ARAMA] Baz kanalda (2446 MHz) \"kho-merhaba\" sinyali araniyor...\n"));
}

void loop() {
  unsigned long suan = millis();

  // 1. SENKRON ATLAMA MOTORU (Vericiden 50 ms ÖNCE atlar)
  if (sistemDurumu == SENKRON_KILITLI) {
    if (suan - sonSlotZamani >= SLOT_SURESI) {
      sonSlotZamani += SLOT_SURESI;
      hopIndex = (hopIndex + 1) % TOPLAM_KANAL;

      // Yeni frekansa geçip pusuya yat:
      kanalDegistir(hopChannels[hopIndex]);
      kacirilanSlotSayisi++;

      // 50 kanallı sistemde 15 slot boyunca (~15 sn) sinyal alınamazsa baz frekansa dön:
      if (kacirilanSlotSayisi >= 15) {
        Serial.println(F("\n>>> [SENKRON KOPTU ❌] Sinyal bulunamadi. Baz kanala (2446 MHz) donuluyor..."));
        sistemDurumu = SENKRON_ARANIYOR;
        hopIndex = 0;
        kacirilanSlotSayisi = 0;
        kanalDegistir(hopChannels[0]);
      }
    }
  }

  // 2. PAKET ALMA, DEŞİFRE VE PAKET BAŞARIM ORANI HESABI
  if (radio.available()) {
    unsigned long paketYakalamaAni = millis();

    byte gelenSifreli[32] = {0};
    SenkronPaket paket;

    // 32 baytlık şifreli paketi oku
    radio.read(&gelenSifreli, 32);

    // AES-256 Deşifre (2x16 bayt blok)
    aes256.decryptBlock((byte*)&paket, gelenSifreli);
    aes256.decryptBlock(((byte*)&paket) + 16, gelenSifreli + 16);

    // Güvenlik Doğrulaması: "kho-merhaba" ile başlıyor mu?
    if (strncmp(paket.metin, "kho-merhaba", 11) == 0) {
      digitalWrite(LED_PIN, HIGH);

      // İlk kilitlenme anı
      if (sistemDurumu == SENKRON_ARANIYOR) {
        Serial.println(F("\n>>> [SENKRON KİLİTLENDİ ✔] 50 Kanallı frekans atlama döngüsüne girildi!\n"));
        sistemDurumu = SENKRON_KILITLI;
        pencereBaslangicSlot = paket.slotNo;
        pencereAlinanPaket = 0;
      }

      // SAAT HİZALAMA (Underflow Korumalı):
      // Alıcı, vericiden 50 ms erken geçecek şekilde saatini hizalar
      if (paketYakalamaAni >= ERKEN_GECIS_AVANSI) {
        sonSlotZamani = paketYakalamaAni - ERKEN_GECIS_AVANSI;
      } else {
        sonSlotZamani = 0;
      }

      hopIndex = paket.hopIndeksi;
      kacirilanSlotSayisi = 0;

      // Mükerrer okuma filtresi (Her slottaki paketi 1 kez say)
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
        Serial.print(F("/49] | \""));
        Serial.print(ekranaBas);
        Serial.println(F("\""));

        // --- 100 PAKETLİK BAŞARI ORANI RAPORU ---
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
 *    - 2402 MHz ile 2524 MHz arasındaki 122 MHz'lik geniş spektrum bandına yayılan
 *      50 farklı kanalda her saniye (1000 ms) bir frekans değiştirerek yüksek
 *      yayılı spektrum kazancı (Process Gain) elde etmeyi,
 *    - Underflow korumalı 50 ms erken geçiş avansı ile uzun süreli çalışmalarda
 *      kesintisiz senkronizasyon ve 100 paketlik PDR analizi sunmayı hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Uzun menzilli ISM bandı telemetri modülleri (LoRa FHSS / LR-FHSS benzeri),
 *      akıllı şebeke sayaç okuma (AMI) sistemleri ve geniş alana yayılmış endüstriyel
 *      sensör ağlarında hem yasal güç/dwell-time sınırlarına uymak hem de kanal
 *      tıkanıklığını aşmak için kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - Düşük Yakalanma / Tespit Edilme Olasılıklı (LPI / LPD - Low Probability of
 *      Intercept / Detection) Muhabere: Yayın enerjisi 50 farklı kanala (122 MHz
 *      bant genişliğine) dağıtıldığı için, düşman spektrum analizörlerinin ve
 *      yön bulucu (DF - Direction Finding) sistemlerin yayını tespit edip kaynağın
 *      konumunu üçgenlemesi (triangulation) büyük ölçüde zorlaşır.
 *    - Geniş Bant Baraj Karıştırmaya (Barrage Jamming) Karşı Direnç: Bir karıştırıcının
 *      50 kanalın tamamını aynı anda bastırabilmesi için gücünü 122 MHz'e bölmesi
 *      gerekir; bu da karıştırıcının kanal başına düşen efektif gücünü (J/S oranını)
 *      50 kat düşürerek dost haberleşmenin devam etmesini sağlar.
 * ============================================================================ */
