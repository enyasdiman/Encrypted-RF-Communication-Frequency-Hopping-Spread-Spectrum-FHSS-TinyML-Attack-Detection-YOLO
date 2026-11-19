// ============================================================================
// BAŞLIK    : Wi-Fi UDP Paket Düşürme (Drop Attack) Simülatörü ve İstemci (Client/TX)
// DOSYA ADI : 14_WiFi_UDP_Drop_Atak_Simulatoru_Verici_Client.ino
// HAZIRLAYAN: Eda Nur YAŞDIMAN (IHASresearch)
// AÇIKLAMA  : ESP32 veya ESP8266 üzerinde çalışan Wi-Fi UDP İstemci (Client) kodu.
//             Sunucuya (Server/AP) saniyede 100 adet (10 ms aralıkla) sıra numaralı
//             (seq) ve etiketli (label: 0=Normal, 1=Atak) UDP paketi gönderir.
//             30 saniye Normal İletişim, 30 saniye Paket Düşürme (Drop Attack)
//             döngüsü uygular. Atak fazında paketlerin %20-%80'ini kasıtlı düşürür,
//             TX çıkış gücünü en düşüğe çeker ve PHY modülasyon hızını zorlayarak
//             gerçekçi fiziksel katman (RSSI düşüşü ve 802.11 retry artışı) bozulması
//             üretir. Ayrıca Seri Port üzerinden manuel mod kontrolünü destekler.
// ============================================================================

#if defined(ESP8266)
  #include <ESP8266WiFi.h>  // ESP8266 Wi-Fi kütüphanesi
#else
  #include <WiFi.h>         // ESP32 Wi-Fi kütüphanesi
  #include "esp_wifi.h"     // ESP32 alt seviye 802.11 PHY/MAC kontrol kütüphanesi
#endif
#include <WiFiUdp.h>        // UDP soket haberleşme kütüphanesi

// ========================== HABERLEŞME VE ATAK AYARLARI ==========================
#define AP_SSID        "UAV_LINK"   // Sunucu (15 ve 20 numaralı alıcılar) ESP32'nin kurduğu Wi-Fi AP adı
#define AP_PASS        "uavlink123" // Wi-Fi AP WPA2 parolası
#define SERVER_PORT    4210         // UDP hedef port numarası
#define GONDERME_MS    10           // Paket gönderim periyodu: 10 ms (Saniyede 100 paket / 100 Hz)
#define NORMAL_MS      30000        // Normal haberleşme faz süresi: 30.000 ms (30 saniye)
#define ATAK_MS        30000        // Paket düşürme (Drop Attack) faz süresi: 30.000 ms (30 saniye)
#define DUSURME_MIN    20           // Atak fazında saniyelik minimum paket düşürme yüzdesi (%20)
#define DUSURME_MAX    80           // Atak fazında saniyelik maksimum paket düşürme yüzdesi (%80)
// =================================================================================

// Sunucu ile BİREBİR AYNI 5 Baytlık UDP Paket Yapısı (Bellekte hizalama boşluğu olmadan)
struct __attribute__((packed)) Paket {
  uint32_t seq;    // 4 Bayt: Her adımda artan paket sıra numarası (düşürülen paketlerde de artar)
  uint8_t  label;  // 1 Bayt: O anki fazın gerçek etiketi (0 = Normal İletişim, 1 = Drop Atağı)
};

WiFiUDP  udp;                      // UDP istemci nesnesi
uint32_t seq          = 0;         // Genel paket sıra numarası sayacı
bool     atak         = false;     // Aktif faz durumu (false = Normal, true = Drop Atağı)
bool     otomatikMod  = true;      // true = 30sn Normal / 30sn Atak döngüsü, false = Manuel kontrol
uint32_t fazBaslangic = 0;         // Mevcut fazın başladığı zaman damgası (ms)
uint8_t  dusurmeOrani = 0;         // Aktif saniye için geçerli paket düşürme oranı (%)
uint32_t gonderilen   = 0;         // 1 saniyelik pencerede başarıyla gönderilen paket sayısı
uint32_t dusurulen    = 0;         // 1 saniyelik pencerede kasıtlı düşürülen paket sayısı
uint32_t toplamGonderilen = 0;     // Açılıştan itibaren gönderilen toplam paket
uint32_t toplamDusurulen  = 0;     // Açılıştan itibaren düşürülen toplam paket

// ----------------------------------------------------------------------------
// FONKSİYON : radyoAyarla
// GÖREVİ    : Normal ve Atak fazlarına göre Wi-Fi verici gücünü (TX Power) ve
//             802.11 fiziksel veri hızını (PHY Rate) donanımsal olarak değiştirir.
//             Atak modunda güç düşürülüp hız (MCS7 / 54 Mbps) artırılarak
//             alıcı tarafta gerçek RSSI dalgalanması ve MAC retry artışı yaratılır.
// ----------------------------------------------------------------------------
void radyoAyarla(bool atakModu) {
#if defined(ESP8266)
  // ESP8266: Güç normalde 20.5 dBm (maksimum), atakta 0 dBm (minimum)
  // PHY Hızı: Normalde otomatik, atakta 54 Mbps OFDM sabitlenerek hata oranı artırılır
  WiFi.setOutputPower(atakModu ? 0.0f : 20.5f);
  if (atakModu) wifi_set_user_fixed_rate(FIXED_RATE_MASK_STA, PHY_RATE_54);
  else          wifi_set_user_fixed_rate(FIXED_RATE_MASK_NONE, PHY_RATE_54);
#else
  // ESP32: Güç normalde 19.5 dBm (maksimum), atakta -1 dBm (minimum)
  // PHY Hızı: Normalde kararlı 1 Mbps (DSSS), atakta yüksek hızlı MCS7 Short-GI
  WiFi.setTxPower(atakModu ? WIFI_POWER_MINUS_1dBm : WIFI_POWER_19_5dBm);
  esp_wifi_config_80211_tx_rate(WIFI_IF_STA, atakModu ? WIFI_PHY_RATE_MCS7_SGI : WIFI_PHY_RATE_1M_L);
#endif
}

// ----------------------------------------------------------------------------
// FONKSİYON : fazDegistir
// GÖREVİ    : İstemciyi Normal (0) veya Drop Attack (1) fazına geçirir ve
//             radyo parametrelerini günceller.
// ----------------------------------------------------------------------------
void fazDegistir(bool yeniAtakDurumu) {
  atak = yeniAtakDurumu;
  fazBaslangic = millis();
  radyoAyarla(atak);
  if (atak) {
    Serial.println("# >>> [FAZ GEÇİŞİ] DROP ATAĞI BAŞLADI (Etiket=1, Düşük TX Gücü, %20-%80 Paket Düşürme)");
  } else {
    Serial.println("# <<< [FAZ GEÇİŞİ] NORMAL İLETİŞİME GEÇİLDİ (Etiket=0, Tam TX Gücü, %0 Paket Düşürme)");
  }
}

void setup() {
  Serial.begin(115200);               // 115200 baud seri haberleşme
  delay(500);

  WiFi.mode(WIFI_STA);                // İstemciyi İstasyon (Station / Client) moduna al

#if defined(ESP8266)
  randomSeed(RANDOM_REG32);           // ESP8266 donanımsal rastgele sayı üreteci tohumu
  WiFi.setSleepMode(WIFI_NONE_SLEEP); // Paket aralıklarının (gap_ms) bozulmaması için uyku modunu kapat
#else
  randomSeed(esp_random());           // ESP32 donanımsal RNG tohumu
  WiFi.setSleep(false);               // Modem uyku modunu kapat (sabit 10 ms paket ritmi için kritik)
#endif

  WiFi.setAutoReconnect(true);        // Bağlantı koparsa otomatik yeniden bağlan
  WiFi.begin(AP_SSID, AP_PASS);       // Sunucu AP'ye ("UAV_LINK") bağlanmayı başlat

  Serial.print("\n# [CLIENT] Sunucu AP'ye baglaniliyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }

  Serial.printf("\n# [BAGLANDI] Client IP: %s | Server (Gateway) IP: %s | Port: %d\n",
                WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), SERVER_PORT);
  Serial.printf("# [TAKVIM] %d sn NORMAL / %d sn DROP ATAGI dongusu. Baslangic: NORMAL\n",
                NORMAL_MS / 1000, ATAK_MS / 1000);
  Serial.println("# [KOMUTLAR] 'a'=Manuel Atak, 'n'=Manuel Normal, 'o'=Otomatik Dongu, 'r'=Sayaclari Sifirla");

  radyoAyarla(false);                 // Başlangıçta normal radyo ayarlarını uygula
  fazBaslangic = millis();            // Faz sayacını başlat
}

void loop() {
  uint32_t simdi = millis();

  // 1. SERİ PORT ÜZERİNDEN MANUEL KONTROL KOMUTLARI
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'a' || c == 'A') {
      otomatikMod = false;
      fazDegistir(true);
    } else if (c == 'n' || c == 'N') {
      otomatikMod = false;
      fazDegistir(false);
    } else if (c == 'o' || c == 'O') {
      otomatikMod = true;
      fazBaslangic = simdi;
      Serial.println("# [MOD] Otomatik 30sn Normal / 30sn Atak dongusu aktif.");
    } else if (c == 'r' || c == 'R') {
      toplamGonderilen = toplamDusurulen = 0;
      Serial.println("# [SIFIRLANDI] Kumulatif paket sayaclari sifirlandi.");
    }
  }

  // 2. OTOMATİK FAZ DEĞİŞİM KONTROLÜ (30 sn Normal <-> 30 sn Drop Atağı)
  if (otomatikMod) {
    if (!atak && simdi - fazBaslangic >= NORMAL_MS) {
      fazDegistir(true);
    } else if (atak && simdi - fazBaslangic >= ATAK_MS) {
      fazDegistir(false);
    }
  }

  // 3. SANİYELİK DURUM, BAŞARIM ORANI (PDR) VE YENİ DÜŞÜRME ORANI RAPORU
  static uint32_t sonSaniye = 0;
  if (simdi - sonSaniye >= 1000) {
    sonSaniye = simdi;
    uint32_t saniyeToplam = gonderilen + dusurulen;
    float anlikIletimOrani = saniyeToplam ? ((float)gonderilen / saniyeToplam) * 100.0f : 100.0f;

    // Atak fazındayken her saniye %20 ile %80 arasında rastgele yeni bir düşürme oranı seç
    dusurmeOrani = atak ? random(DUSURME_MIN, DUSURME_MAX + 1) : 0;

    Serial.printf("# [%s] gonderilen=%lu dusurulen=%lu iletim_basarimi=%%%.1f hedef_drop=%%%u | toplam_tx=%lu\n",
                  atak ? "ATAK" : "norm",
                  (unsigned long)gonderilen,
                  (unsigned long)dusurulen,
                  anlikIletimOrani,
                  dusurmeOrani,
                  (unsigned long)toplamGonderilen);
    gonderilen = dusurulen = 0;
  }

  // 4. SABİT ZAMANLI (10 ms / 100 Hz) UDP PAKET GÖNDERİMİ
  static uint32_t sonGonderim = 0;
  if (simdi - sonGonderim < GONDERME_MS) return;
  sonGonderim = simdi;

  if (WiFi.status() != WL_CONNECTED) return;

  // Sıra numarası (seq) paket düşürülse bile her 10 ms'de bir artırılır.
  // Böylece alıcı (Server) aradaki sıra numarası boşluklarından (seq gap) düşen paketleri tespit eder.
  seq++;

  // Atak fazındaysak belirlenen olasılıkla paketi havaya göndermeden düşür (Drop Attack)
  if (atak && random(100) < dusurmeOrani) {
    dusurulen++;
    toplamDusurulen++;
    return;   // Paket düşürüldü (UDP soketine yazılmadı)
  }

  // Paketi hazırla (Sıra No + Gerçek Faz Etiketi) ve Sunucuya (Gateway IP) UDP ile gönder
  Paket p = { seq, (uint8_t)(atak ? 1 : 0) };
  udp.beginPacket(WiFi.gatewayIP(), SERVER_PORT);
  udp.write((const uint8_t *)&p, sizeof(p));
  udp.endPacket();

  gonderilen++;
  toplamGonderilen++;
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - İstemci (Client) ve Sunucu (Server) arasındaki Wi-Fi UDP haberleşme hattında
 *      hem **Normal İletişim (Etiket=0)** hem de **Paket Düşürme / Drop Attack (Etiket=1)**
 *      senaryolarını gerçek zamanlı olarak simüle etmeyi,
 *    - Sadece yazılımsal paket silmekle kalmayıp, atak esnasında radyo verici gücünü
 *      (TX Power) en düşüğe çekerek ve 802.11 modülasyon hızını (MCS7 / 54 Mbps)
 *      zorlayarak fiziksel katmanda (PHY) gerçek RSSI çökmesi, gürültü ve MAC katmanı
 *      yeniden gönderim (retry) artışı oluşturmayı,
 *    - Alıcı tarafta eğitilecek **TinyML (Yapay Sinir Ağı)** modeli için otomatik
 *      etiketlenmiş (ground-truth labeled) veri akışı üretmeyi hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Kablosuz IoT ağlarında, endüstriyel telemetri sistemlerinde ve otonom araç
 *      haberleşmelerinde ağ dayanıklılık testi (Network Chaos Engineering / Stress Test),
 *    - Makine öğrenmesi tabanlı Saldırı Tespit Sistemleri (IDS - Intrusion Detection
 *      System) için kontrollü anomali ve paket kaybı veri seti (dataset) üretimi.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - İHA / SİHA ve Sürü İHA (Swarm UAV) Elektronik Harp Simülatörü (Red Team Node):
 *      Askeri tatbikatlarda veya laboratuvar testlerinde, bir İHA veri linkinin
 *      düşman Elektronik Taarruz (EA / Jamming), Seçici Paket Düşürme (Selective
 *      Forwarding / Grayhole Attack) veya Düşük Güçlü Karıştırma altında nasıl
 *      davrandığını test etmek için kullanılır.
 *    - Bilişsel Radyo (Cognitive Radio) ve Gömülü Yapay Zeka Eğitimi: Taktik saha
 *      modemlerinin normal kanal zayıflaması ile kasıtlı siber/elektronik saldırıyı
 *      birbirinden ayırt edebilmesi için gereken eğitim verisini üretir.
 * ============================================================================ */
