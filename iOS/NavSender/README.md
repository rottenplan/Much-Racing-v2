# NavSender — App iOS Navigasi untuk MuchRacing

App iPhone (SwiftUI + CoreBluetooth + MapKit + CoreLocation) yang **menghitung
rute sungguhan** lalu mengirim instruksi belokan (turn-by-turn) ke device
MuchRacing lewat Bluetooth LE.

Device mengiklankan service **Nordic UART Service (NUS)** dengan nama
**"MuchRacing-Nav"**. App terhubung ke service itu dan menulis JSON ke
karakteristik RX (`6E400002-…`).

## Fitur

- **Navigasi live:** ketik tujuan (alamat/nama tempat) → app menghitung rute
  berkendara lewat **MapKit** (gratis, tanpa API key) → tiap langkah
  diklasifikasikan jadi kode manuver (lurus, belok kiri/kanan, tajam, putar
  balik, bundaran, tiba) → dikirim ke device.
- **Auto-advance:** GPS iPhone memantau posisi; saat pengendara < 25 m dari
  titik belokan, langkah berikutnya otomatis terkirim ke device. Layar device
  juga di-"keep alive" (kirim ulang tiap 45 detik) agar tidak idle saat
  berhenti lama.
- **Mode manual:** tombol arah siap pakai + stepper jarak + JSON custom untuk
  uji coba tanpa GPS.
- Peta rute + penanda titik awal/hijau dan tujuan/merah di iPhone.

## Isi folder

```
NavSender/
├── NavSenderApp.swift          <- entry point app
├── ContentView.swift           <- layar utama: koneksi BLE, cari tujuan, mulai navigasi
├── BLEManager.swift            <- CoreBluetooth (scan, connect, kirim JSON)
├── RouteManager.swift          <- MapKit: cari tempat, hitung rute, klasifikasi manuver
├── LocationTracker.swift       <- CoreLocation: posisi live untuk auto-advance
├── NavigationSessionView.swift <- layar navigasi: instruksi, peta, jarak, tombol langkah
└── ManualTestSection.swift     <- tombol manuver manual + JSON custom
```

## Syarat build

- **Mac** dengan **Xcode 15+** (Windows tidak bisa build app iOS)
- iPhone dengan **iOS 16+**, Bluetooth & **Location Services** aktif
- Apple ID gratis sudah cukup untuk menjalankan di iPhone sendiri

## Cara build di Xcode (sekali saja)

1. Buka **Xcode** → `File` → `New` → `Project` → pilih **iOS → App** → `Next`
2. **Product Name**: `NavSender`
   - **Interface**: `SwiftUI`, **Language**: `Swift`
   - Hapus centang `Use Core Data` / `Include Tests` → `Next` → simpan
3. Di Finder, salin **7 file Swift** dari folder `NavSender/` ini ke folder
   proyek Xcode Anda (timpa `NavSenderApp.swift` & `ContentView.swift` yang
   dibuat template, tambahkan sisanya). Jika tidak muncul di project
   navigator: drag ke Xcode → centang target `NavSender` → `Finish`.
4. **Tambahkan dua izin** di target **NavSender** → tab **Info**:
   - **Privacy - Bluetooth Always Usage Description**
     → `Membutuhkan Bluetooth untuk terhubung ke MuchRacing-Nav`
   - **Privacy - Location When In Use Usage Description**
     → `Dibutuhkan untuk melacak posisi selama navigasi`
5. **Signing:** target **NavSender** → tab **Signing & Capabilities** → pilih
   **Team** (Apple ID Anda).
6. Hubungkan iPhone ke Mac → pilih iPhone sebagai run destination →
   tekan **Cmd + R**.

## Cara pakai

1. Di device: buka menu utama → **halaman 2** → **NAVIGATION**
2. Buka app **NavSender** → tunggu status **"Terhubung ke MuchRacing-Nav ✓"**
   (tekan **Scan / Ulangi** jika perlu)
3. Ketik **tujuan** di kolom (mis. `Monas Jakarta`, `Jl. Sudirman No. 1`)
   → tekan **"Cari & Hitung Rute"** → setujui izin lokasi jika diminta
4. App menampilkan ringkasan rute (jarak, waktu, jumlah langkah)
   → tekan **"Mulai Navigasi"**
5. Layar navigasi menampilkan instruksi + peta + jarak ke belokan. Device
   menunjukkan panah besar + jarak + instruksi yang sama, disertai bunyi beep
   tiap ganti arah. Saat mendekati titik belokan, langkah berikutnya otomatis
   terkirim.
6. **"Akhiri Navigasi"** → device kembali ke layar idle.

### Format JSON yang dikirim

Tiap langkah dikirim sebagai:

```json
{"icon":6,"dist":300,"text":"Turn right onto Jl. Melati"}
```

Kode `icon`: `0`=tiba · `1`=lurus · `2`=kiri sedikit · `3`=kiri · `4`=kiri tajam
· `5`=kanan sedikit · `6`=kanan · `7`=kanan tajam · `8`=putar balik · `9`=bundaran

Icon manuver dihitung otomatis dari **perubahan arah** (bearing) antar langkah
rute MapKit. Langkah terakhir selalu `icon 0` ("You have arrived").

## Troubleshooting

| Masalah | Solusi |
|---|---|
| App tidak menemukan "MuchRacing-Nav" | Pastikan layar device sedang di **NAVIGATION**; tekan **Scan / Ulangi**; matikan-nyalakan Bluetooth iPhone |
| Pesan izin Bluetooth | **Settings → Privacy & Security → Bluetooth** → aktifkan NavSender |
| Pesan izin lokasi | **Settings → Privacy & Security → Location Services** → aktifkan untuk NavSender |
| "Rute tidak ditemukan" | Pastikan tujuan punya nama jalan/alamat yang valid; coba penyebutan lain |
| Terhubung tapi device tidak bereaksi | Device harus membuka layar **NAVIGATION**; chip pojok kanan bawah harus **BT LINK** |
| Jarak di device tidak berkurang | Auto-advance butuh sinyal GPS; gunakan tombol **"Langkah Berikutnya"** secara manual jika GPS lemah |

## Catatan

- **MapKit** dipakai untuk rute (gratis, bawaan iOS, tanpa API key). Google
  Directions API butuh API key dan tidak diperlukan di sini.
- iOS melarang app membaca notifikasi app lain, jadi pendekatan "relay Google
  Maps" ala Android tidak bisa diterapkan. App ini menghitung rute sendiri —
  hasilnya justru lebih andal karena tidak bergantung pada Google Maps.
- Navigasi berjalan di foreground; untuk penggunaan di atas motor, pasang
  iPhone di handlebar dengan layar tetap menyala.