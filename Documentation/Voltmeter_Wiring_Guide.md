# 🔌 Panduan Pemasangan Voltmeter Motor (12V)

Panduan lengkap cara memasang voltmeter pada **Much Racing v4.1.3** untuk memantau
tegangan aki motor secara real-time di layar Speedometer.

---

## ⚠️ PERINGATAN PENTING

> **JANGAN PERNAH** menyambungkan 12V langsung ke pin ESP32!
>
> Pin ESP32 hanya tahan **maksimal 3.3V**. Sambungan langsung akan
> **MERUSAK ESP32 secara permanen**.

Selalu gunakan **pembagi tegangan** (voltage divider) seperti yang dijelaskan di bawah.

---

## 🛒 Komponen yang Dibutuhkan (Total ~Rp 5.000)

| Komponen | Nilai | Warna Gelang | Fungsi |
| ---------- | ------- | -------------- | -------- |
| Resistor **R1** | **33 kΩ** | Oranye-Oranye-Oranye-Emas | Menurunkan tegangan (atas) |
| Resistor **R2** | **5.6 kΩ** | Hijau-Biru-Merah-Emas | Referensi GND (bawah) |
| Kabel jumper | 3 buah | — | Sambungan |
| Multimeter | — | — | Untuk kalibrasi |

---

## 🔐 Skema Rangkaian

```
   Kabel (+) Aki Motor 12V
          │
          │   ← R1 = 33kΩ
          │
          ●━━━━━━━━━━━━━→ KABEL A → GPIO36 (ESP32)
          │
          │   ← R2 = 5.6kΩ
          │
          │
   Kabel (−) Aki Motor ━━━━→ GND (ESP32)
```

### Langkah Perakitan

1. **Sambungkan** kaki resistor R1 dan R2 bersama-sama (bentuk satu titik temu)
2. Kaki **bebas R1** → ke kabel **positif (+)** aki motor
3. **Titik temu** R1-R2 → ke kabel A (menuju **GPIO36** ESP32)
4. Kaki **bebas R2** → ke **GND** ESP32
5. **GND ESP32** juga harus **satu ground** dengan negatif aki motor!

> 💡 Titik temu R1-R2 adalah "titik tengah" — di sinilah 12V sudah turun
> menjadi ~2V yang **aman** untuk ESP32.

### Perhitungan Rasio

```
Rasio = (R1 + R2) / R2
      = (33.000 + 5.600) / 5.600
      = 6.89
```

| Tegangan Aki | Tegangan di GPIO36 |
| -------------- | ------------------- |
| 12.0 V | 1.74 V ✅ |
| 12.6 V | 1.83 V ✅ |
| 14.4 V | 2.09 V ✅ |
| 22.0 V (maks aman) | 3.19 V ✅ |

---

## 📍 Menemukan Pin GPIO36

Board **Sunton/ESP32-3248S035C**:

- GPIO36 biasanya berlabel **VP** atau **GPIO36/SVP** di header
- Letaknya berdampingan dengan GPIO35 (RPM) dan GPIO34 (baterai)
- Cek silkscreen di board: baris pin `34 | 35 | 36(VP) | 39(VN)`

**Konfigurasi firmware** (`Firmware/src/config.h`):

```cpp
#define PIN_VOLTMETER 36
#define VOLTMETER_RATIO 6.89f // (R1+R2)/R2
#define VOLTMETER_SAMPLES 16  // jumlah sampel untuk rata-rata
```

> Jika GPIO36 sudah terpakai, ubah `PIN_VOLTMETER` ke pin lain
> (misal GPIO39/VN) — pastikan tetap di ADC1.

---

## ✅ Prosedur Uji Coba Bertahap (WAJIB!)

### 1. Uji rangkaian TANPA ESP32

Rakit rangkaian, sambungkan ke aki, lalu **ukur titik tengah dengan multimeter**:

- Hasil harus: **~1.8V – 2.1V** (jika aki 12.4–14.4V)
- Jika terbaca **12V** → rangkaian SALAH! Perbaiki dulu sebelum
  menyambungkan ke ESP32.

### 2. Sambungkan ke ESP32

Setelah yakin titik tengah < 3.3V, sambungkan kabel A ke GPIO36.

### 3. Nyalakan motor

Panel **VOLTMETER** di layar Speedometer akan menampilkan tegangan.

---

## 🎯 Kalibrasi Agar Angka Akurat

Bandingkan tampilan layar vs multimeter di terminal aki:

```
RATIO_BARU = RATIO_LAMA × (voltase_multimeter ÷ voltase_layar)
```

**Contoh:**

- Layar menampilkan `12.6`, multimeter menunjukkan `12.4` → terlalu tinggi
- `RATIO_BARU = 6.89 × (12.4 ÷ 12.6) = 6.78`

Edit di `Firmware/src/config.h`:

```cpp
#define VOLTMETER_RATIO 6.78f  // hasil kalibrasi
```

Lalu build & upload ulang. Ulangi sampai selisih **< 0.1V**.

---

## 📊 Interpretasi Pembacaan Voltase

| Layar menunjukkan | Kondisi |
| --- | --- |
| **0.0V** | Kabel belum tersambung / rangkaian belum dipasang |
| **11.8–12.8V** (motor mati) | ✅ Aki sehat & penuh |
| **13.5–14.6V** (motor hidup) | ✅ Spul/alternator bekerja normal |
| **< 12.0V** | 🔴 Aki lemah atau spul rusak |
| **> 15.0V** | 🟡 Regulator overcharge — periksa segera |

---

## 🧠 Cara Kerja Firmware

1. ESP32 membaca **ADC 12-bit** di GPIO36: nilai `0–4095` = tegangan `0–3.3V`
2. Aki motor `12–14.5V` **melebihi batas** → harus diturunkan oleh pembagi tegangan
3. Pembagi R1+R2 membagi 12V ÷ 6.89 → **~2.1V** (aman)
4. Firmware mengalikan kembali hasil baca × 6.89 → voltase asli aki
5. **Smoothing 70/30** + **rata-rata 16 sampel** → angka stabil tanpa jitter
6. Redraw hanya saat nilai berubah > 0.15V (anti-flicker)

Kode terletak di `Firmware/src/ui/screens/SpeedometerScreen.cpp`:

```cpp
float readMotorVoltage() {
  // 16 sampel ADC → rata-rata → konversi → smoothing
  float volt = (avg / 4095.0f) * 3.3f * VOLTMETER_RATIO;
  filtered = filtered * 0.7f + volt * 0.3f;
  return filtered;
}
```

---

## ⚡ Ringkasan 5 Poin Penting

1. ✅ Gunakan **R1 = 33kΩ (atas)** dan **R2 = 5.6kΩ (bawah)**
2. ⚠️ **JANGAN** sambungkan 12V langsung ke GPIO36!
3. 🔗 **GND ESP32 harus digabung** dengan negatif aki motor
4. 🧪 **Uji dengan multimeter dulu** sebelum menyambungkan ke ESP32
5. 🎛️ Kalibrasi lewat `VOLTMETER_RATIO` di `config.h`

---

## 🔗 Dokumentasi Terkait

- [RPM Wiring Guide](RPM_Wiring_Guide.md) — pemasangan sensor RPM
- [Firmware Features](FirmwareFeatures.md) — daftar fitur firmware
- [Menu Structure](MENU_STRUCTURE.md) — struktur menu navigasi
