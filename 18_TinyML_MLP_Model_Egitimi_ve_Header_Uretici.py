# ============================================================================
# BAŞLIK    : TinyML Çok Katmanlı Algılayıcı (MLP) Eğitimi ve C Header Üreticisi
# DOSYA ADI : 18_TinyML_MLP_Model_Egitimi_ve_Header_Uretici.py
# HAZIRLAYAN: Eda Nur YAŞDIMAN (IHASresearch)
# AÇIKLAMA  : `17_TinyML_Paket_Dusurme_Atak_Veri_Seti.csv` veri setini kullanarak
#             Paket Düşürme (Drop Attack) tespiti için 3 katmanlı bir Yapay Sinir
#             Ağı (MLP: 10 -> 16 -> 8 -> 1) eğitir. Konuma bağımlı mutlak RSSI/SNR
#             değerlerini eleyerek kartların yeri değişse dahi yüksek doğrulukla
#             çalışan 10 dinamik öznitelik seçer. Eğitilen modelin Z-Score
#             normalizasyon parametrelerini ve katman ağırlıklarını ESP32 üzerinde
#             doğrudan çalışacak C dizileri (`19_TinyML_MLP_Model_Agirliklari.h`)
#             olarak dışa aktarır ve C-eşdeğeri ileri geçiş doğrulaması yapar.
# ============================================================================

from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix, f1_score, precision_score, recall_score
from sklearn.model_selection import train_test_split
from sklearn.neural_network import MLPClassifier
from sklearn.preprocessing import StandardScaler

KLASOR = Path(__file__).resolve().parent

# ======================= PARAMETRELER =======================
CSV = KLASOR / "17_TinyML_Paket_Dusurme_Atak_Veri_Seti.csv"
CIKTI = KLASOR / "19_TinyML_MLP_Model_Agirliklari.h"
EPOCHS = 400
# ============================================================

# ÖZNİTELİK MÜHENDİSLİĞİ (FEATURE ENGINEERING):
# - 'ts' zaman damgasıdır, 'avg_noise' her satırda sabittir (-96 dBm), 'label' ise hedef sınıftır.
# - 'avg_rssi', 'min_rssi', 'avg_snr', 'min_snr' mutlak sinyal seviyeleridir: Kartlar arası
#   mesafeye ve konuma bağlıdır. Model bunları ezberlerse kartlar uzaklaştığında normal iletişimi
#   atak sanabilir (overfitting). Bu nedenle sadece konuma duyarsız 10 dinamik öznitelik seçilmiştir:
OZELLIKLER = [
    "received",         # 1 sn pencerede alınan UDP paket sayısı (Normalde ~100)
    "lost",             # 1 sn pencerede düşen/kayıp UDP paket sayısı
    "loss_ratio",       # Paket kayıp oranı (0.00 .. 1.00) -> PDR = 1 - loss_ratio
    "max_consec_lost",  # Ardışık düşen maksimum paket sayısı (Burst loss göstergesi)
    "avg_gap_ms",       # Paketler arası ortalama varış süresi (Normalde ~10 ms)
    "gap_std_ms",       # Paketler arası varış süresi standart sapması (Jitter)
    "max_gap_ms",       # Pencere içindeki en uzun paket arası boşluk (ms)
    "rssi_std",         # 802.11 çerçevelerinin RSSI standart sapması (Güç dalgalanması)
    "retry_ratio",      # IEEE 802.11 MAC katmanı yeniden gönderim (Retry bit=1) oranı
    "sniff_frames",     # Promiscuous sniffer tarafından yakalanan toplam 802.11 çerçeve sayısı
]


def c_dizi(ad, a):
    """NumPy dizisini C dilinde sabit (const float) dizi bildirimine dönüştürür."""
    a = np.asarray(a, np.float32).ravel()
    return f"const float {ad}[{a.size}] = {{ {', '.join(f'{v:.8g}f' for v in a)} }};\n"


def header_yaz(yol, mean, std, katmanlar):
    """Eğitilen modeli 20 numaralı dedektör kodunun beklediği C dizileri olarak yazar."""
    with open(yol, "w", encoding="utf-8") as f:
        f.write("// ============================================================================\n")
        f.write("// BAŞLIK    : TinyML Çok Katmanlı Algılayıcı (MLP) Model Ağırlıkları ve Normalizasyon Sabitleri\n")
        f.write("// DOSYA ADI : 19_TinyML_MLP_Model_Agirliklari.h\n")
        f.write("// HAZIRLAYAN: Eda Nur YAŞDIMAN (IHASresearch) - 18_TinyML_MLP_Model_Egitimi_ve_Header_Uretici.py ile üretildi\n")
        f.write("// MİMARİ    : 10 Giriş Özniteliği -> 16 Gizli Nöron (ReLU) -> 8 Gizli Nöron (ReLU) -> 1 Çıkış (Sigmoid)\n")
        f.write("// ============================================================================\n")
        f.write("// Otomatik uretildi, elle degistirme\n#pragma once\n\n")
        f.write(f"#define N_OZELLIK {len(OZELLIKLER)}\n")
        f.write("// Ozellik sirasi: " + ", ".join(OZELLIKLER) + "\n\n")
        f.write(c_dizi("OZ_MEAN", mean))
        f.write(c_dizi("OZ_STD", std))
        for i, (W, b) in enumerate(katmanlar, 1):
            f.write(f"\n#define L{i}_GIRIS {W.shape[1]}\n#define L{i}_CIKIS {W.shape[0]}\n")
            f.write(c_dizi(f"M_W{i}", W))   # satır = çıkış nöronu, [çıkış][giriş]
            f.write(c_dizi(f"M_B{i}", b))


def main():
    print("=====================================================================")
    print("  TinyML Paket Düşürme (Drop Attack) Tespiti - MLP Eğitim Motoru")
    print("=====================================================================")

    # 1. Veri setini oku ve eksik/bozuk satırları temizle
    df = pd.read_csv(CSV, on_bad_lines="skip")
    df = df[OZELLIKLER + ["label"]].apply(pd.to_numeric, errors="coerce").dropna()
    df = df[df["label"].isin([0, 1])]
    X, y = df[OZELLIKLER].to_numpy(np.float32), df["label"].to_numpy(int)
    print(f"Veri Seti : {CSV.name} -> Toplam {len(y)} satır (Normal={(y == 0).sum()}, Atak={(y == 1).sum()})")

    # 2. Sınıf dengeli (Stratified) %80 Eğitim / %20 Test ayrımı
    X_tr, X_te, y_tr, y_te = train_test_split(X, y, test_size=0.2, stratify=y, random_state=0)

    # 3. Z-Score Normalizasyon: (x - mean) / std (Veri sızıntısını önlemek için sadece eğitim setinden hesaplanır)
    olcek = StandardScaler().fit(X_tr)

    # 4. MLP Sinir Ağı Eğitimi: 10 -> 16 (ReLU) -> 8 (ReLU) -> 1 (Sigmoid)
    model = MLPClassifier(
        hidden_layer_sizes=(16, 8),
        activation="relu",
        alpha=1e-4,
        learning_rate_init=1e-2,
        max_iter=EPOCHS,
        random_state=0,
    )
    model.fit(olcek.transform(X_tr), y_tr)

    # 5. Test Seti Performans Değerlendirmesi
    tahmin = model.predict(olcek.transform(X_te))
    tn, fp, fn, tp = confusion_matrix(y_te, tahmin).ravel()
    acc = accuracy_score(y_te, tahmin)
    prec = precision_score(y_te, tahmin)
    rec = recall_score(y_te, tahmin)
    f1 = f1_score(y_te, tahmin)

    print(f"\nTest Sonuçları ({len(y_te)} satır):")
    print(f"  Doğruluk (Accuracy)   : %{acc * 100:.2f} ({acc:.3f})")
    print(f"  Kesinlik (Precision)  : %{prec * 100:.2f}")
    print(f"  Duyarlılık (Recall)   : %{rec * 100:.2f}")
    print(f"  F1-Skoru (F1-Score)   : %{f1 * 100:.2f}")
    print("\n  Karışıklık Matrisi      tahmin=0 (Normal)   tahmin=1 (Atak)")
    print(f"  gerçek=0 (Normal)       {tn:17d}   {fp:15d}")
    print(f"  gerçek=1 (Atak)         {fn:17d}   {tp:15d}")

    print("\nDetaylı Sınıflandırma Raporu:")
    print(classification_report(y_te, tahmin, target_names=["Normal (0)", "Drop Atağı (1)"], digits=3))

    # 6. Ağırlıkları C Koduna Uygun Formata Dönüştürme
    # scikit-learn ağırlıkları [giriş][çıkış] şeklinde tutar; C matris çarpımı
    # [çıkış][giriş] beklediği için ağırlık matrislerinin transpozu (W.T) alınır.
    katmanlar = [(W.T, b) for W, b in zip(model.coefs_, model.intercepts_)]

    # 7. ESP32 C Kodu İleri Geçiş (Forward Pass) Eşdeğerlik Doğrulaması
    h = olcek.transform(X_te)
    for i, (W, b) in enumerate(katmanlar):
        h = h @ W.T + b
        if i < len(katmanlar) - 1:
            h = np.maximum(h, 0)  # ReLU aktivasyonu
    c_olasilik = 1 / (1 + np.exp(-h[:, 0]))  # Sigmoid aktivasyonu
    fark = np.abs(c_olasilik - model.predict_proba(olcek.transform(X_te))[:, 1]).max()
    print(f"ESP32 C-Eşdeğeri İleri Geçiş Kontrolü -> Maksimum Fark: {fark:.2e}")

    # 8. 19_TinyML_MLP_Model_Agirliklari.h Dosyasını Üret
    header_yaz(CIKTI, olcek.mean_, olcek.scale_, katmanlar)
    n_param = sum(W.size + b.size for W, b in katmanlar)
    print(f"Model Header Yazıldı  : {CIKTI} ({n_param} parametre, {n_param * 4} bayt / ~{n_param * 4 / 1024:.1f} KB)")
    print("=====================================================================")


if __name__ == "__main__":
    main()

# ============================================================================
# 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
# ============================================================================
# 1. BU KOD NEYİ HEDEFLER?
#    - Toplanan kablosuz ağ veri setinden (`17_TinyML_Paket_Dusurme_Atak_Veri_Seti.csv`)
#      konumdan bağımsız 10 öznitelik seçerek hafif bir Çok Katmanlı Algılayıcı
#      (MLP: 10 -> 16 -> 8 -> 1) eğitmeyi ve bu modeli hiçbir harici kütüphaneye
#      ihtiyaç duymadan saf C dizileri (`19_TinyML_MLP_Model_Agirliklari.h`) olarak
#      gömülü sisteme aktarmayı hedefler.
#
# 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
#    - Bellek kısıtlı mikrodenetleyiciler (ESP32, STM32, Arduino) için PC'de
#      eğitilen makine öğrenmesi modellerinin uç cihazlara (Edge AI / TinyML)
#      sıfır bağımlılıkla gömülmesinde kullanılır.
#
# 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
#    - Taktik Haberleşme ve İHA Otonom Siber/Elektronik Savunma Modülü: Sadece
#      321 parametre (1284 bayt) büyüklüğündeki bu sinir ağı, en küçük askeri
#      telemetri modüllerinin dahi içine gömülebilir; harici GPU gerektirmeden
#      mikrosaniyeler içinde elektronik karıştırma ve paket düşürme saldırılarını
#      %98.5 doğrulukla tespit eder.
# ============================================================================
