# Gauguin Battery Gauge Fix (LineageOS / Custom ROMs)
### Open-Source Native C Daemon & Magisk Module for Xiaomi Mi 10T Lite / Redmi Note 9 Pro 5G (*gauguin*)

[English](#english) | [Türkçe](#türkçe)

---

<a name="english"></a>
## English

### 📌 Problem Description
On Xiaomi Mi 10T Lite / Redmi Note 9 Pro 5G (*gauguin*) running LineageOS or other AOSP-based custom ROMs, the battery percentage frequently freezes (often around 75%, 79%, or 80%) and the device unexpectedly shuts down when depleted.

#### Technical Root Cause
* In stock MIUI/HyperOS, battery management relies on a proprietary closed-source daemon (`/vendor/bin/batterysecret`).
* This proprietary service periodically authenticates the battery and pushes updated State-of-Charge (SoC) / Open-Circuit Voltage (OCV) lookup data (`udata`) to Qualcomm's Fuel Gauge kernel driver via `/dev/qg`.
* On LineageOS and custom ROMs, `batterysecret` fails to maintain communication with the kernel QG driver. Without fresh calibration packets, the hardware coulomb counter and SoC estimator freeze.
* Official LineageOS bug reports (e.g., Issue #87, #290) were closed without a fix as "third-party battery / hardware issue".

### 💡 The Solution (v4.3 - Monotonic Discharge Filter & Active Sync)
**`gauguin-battery-fix`** is an open-source, standalone native C daemon (`qg-daemon`) packaged as a Magisk module that replaces the broken telemetry path:
1. Directly interacts with Qualcomm's `/dev/qg` hardware character device using native Linux syscalls (`open`, `read`, `write`).
2. Reads hardware OCV measurements (`kdata`).
3. Translates voltage to accurate percentage using the factory battery lookup table (56-point J17 Cosmx 4820mAh profile).
4. **Dynamic IR Compensation:** Calculates internal resistance voltage sag ($V_{OCV} = V_{NOW} + I \cdot R_{INT}$) under CPU load, preventing check suppression during heavy usage.
5. **Monotonic Discharge Filter:** Prevents upward percentage jumps during CPU load variations while discharging, ensuring smooth downward battery decay.
6. Submits calibrated 144-byte `udata` packets back to `/dev/qg` and triggers active Android framework sync (`cmd battery reset`).
7. Runs in the background with zero CPU overhead (60-second periodic checks with 1-hour fail-safe full syncs).

---

### 📦 Installation

#### Method 1: Magisk App (Recommended)
1. Download the latest `gauguin-battery-fix-v4.3.zip` from Releases.
2. Open **Magisk Manager** -> **Modules** -> **Install from storage**.
3. Select the zip file and reboot your device.

#### Method 2: ADB Shell (Manual)
```bash
adb push gauguin-battery-fix-v4.3.zip /data/local/tmp/
adb shell su -c "magisk --install-module /data/local/tmp/gauguin-battery-fix-v4.3.zip"
adb reboot
```

---

### 🛠️ Building from Source
Requires an ARM64 cross-compiler (`aarch64-linux-gnu-gcc`):
```bash
git clone https://github.com/03muhammedsaid-star/gauguin-battery-fix.git
cd gauguin-battery-fix
make
```
This generates the standalone binary `build/qg-daemon` and the flashable Magisk module `build/gauguin-battery-fix-v4.3.zip`.

---

<a name="türkçe"></a>
## Türkçe

### 📌 Sorun Nedir?
Xiaomi Mi 10T Lite / Redmi Note 9 Pro 5G (*gauguin*) cihazlarda LineageOS veya diğer saf Android (AOSP) ROM'ları kurulduğunda, pil göstergesi sık sık belli bir seviyede (%75, %79, %80 gibi) donup kalır ve telefon şarjı bittiğinde aniden kapanır.

#### Kök Neden
* Orijinal MIUI/HyperOS yazılımında pil yönetimi, Xiaomi'nin kapalı kaynaklı **`batterysecret`** servisi tarafından yürütülür.
* Bu servis kerneldeki Qualcomm Fuel Gauge sürücüsünü (`/dev/qg`) düzenli olarak besler.
* LineageOS'ta bu kapalı kaynak servis düzgün çalışmadığı için çip güncel veri alamaz ve donar.
* LineageOS resmi kayıtlarında (Issue #87, #290) bu durum "üçüncü parti pil / donanım arızası" denilerek kapatılmıştır.

### 💡 Çözüm (v4.3 - Monotonik Deşarj Filtresi & Active Sync)
Bu proje, Xiaomi'nin eksik kalan sürücüsünün işini devralan **saf C ile derlenmiş bağımsız bir sistem servisi (`qg-daemon`)** ve **Magisk modülüdür**:
* `/dev/qg` donanım arabiriminden OCV gerilimini okur.
* Orijinal fabrika batarya tablosundan (56 noktalı Cosmx 4820mAh eğrisi) gerçek yüzdeyi hesaplar.
* **Dinamik Akım Kompanzasyonu:** İşlemci yük altındayken çekilen akıma göre oluşan voltaj düşüşünü ($V_{OCV} = V_{NOW} + I \cdot R_{INT}$) matematiksel olarak hesaba katar.
* **Monotonik Deşarj Filtresi:** Deşarj sırasında anlık yük düşüşlerinde pil yüzdesinin yukarı fırlamasını engeller, kararlı ve aşağı yönlü süzülme sağlar.
* Çipe 144 baytlık kalibrasyon paketini iletir ve Android `healthd` önbelleğini yeniler (`cmd battery reset`).
* Arka planda 60 saniyede bir kontrol yapar, %3'ten fazla sapma olursa anında düzeltir.

---

### 📄 Lisans
Bu proje [MIT Lisansı](LICENSE) ile korunmaktadır. Özgürce kullanabilir, çatallayabilir ve paylaşabilirsiniz.