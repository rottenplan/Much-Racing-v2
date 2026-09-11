import SwiftUI

// MARK: - Setelan: WiFi device, IP, BLE manual test, log, tentang

struct SettingsView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var live: LiveStore
    @EnvironmentObject var location: LocationTracker
    @EnvironmentObject var route: RouteManager

    @State private var ipInput = ""

    var body: some View {
        Form {
            Section {
                Text("Hak Akses")
                    .font(.headline)
                    .listRowBackground(Color(.secondarySystemBackground))
            }

            Section("Lokasi (Navigasi)") {
                PermissionRow(
                    title: "Lokasi",
                    detail: locationDetail,
                    dot: location.authorization == .notDetermined ? .gray
                         : (location.isAuthorized ? Color.neonGreen : Color.neonRed),
                    actionTitle: location.authorization == .notDetermined ? "Minta Izin"
                                  : (location.isAuthorized ? nil : "Buka Pengaturan"),
                    action: locationAction
                )
            }

            Section("Bluetooth (Navigasi)") {
                PermissionRow(
                    title: "Bluetooth",
                    detail: bluetoothDetail,
                    dot: ble.centralState == .poweredOn ? Color.neonGreen : Color.neonRed,
                    actionTitle: ble.centralState == .poweredOn ? nil : "Buka Pengaturan",
                    action: openSystemSettings
                )
            }

            Section {
                Text("Device WiFi (Live & Sesi)")
                    .font(.headline)
                    .listRowBackground(Color(.secondarySystemBackground))
            }

            Section("Koneksi WiFi") {
                TextField("Alamat device (mis. http://192.168.4.1)", text: $ipInput)
                    .keyboardType(.URL)
                    .textInputAutocapitalization(.never)
                    .disableAutocorrection(true)
                Button("Simpan Alamat") {
                    let url = ipInput.trimmingCharacters(in: .whitespaces)
                    live.setBaseURL(url.hasPrefix("http") ? url : "http://\(url)")
                    ipInput = APClient.savedBaseURL()
                }
                Button {
                    Task { await live.tick(force: true) }
                } label: {
                    Label("Cek Koneksi", systemImage: "antenna.radiowaves.left.and.right")
                }
                HStack {
                    Text("Status")
                    Spacer()
                    Text(live.connected ? "Terhubung" : "Putus")
                        .foregroundColor(live.connected ? .neonGreen : .secondary)
                }
            }

            Section("Lima Mode Koneksi") {
                Label("AP: iPhone join \u{201C}MuchRacing-GPS\u{201D} (12345678), alamat otomatis 192.168.4.1", systemImage: "1.circle")
                Label("Hotspot/rumah: device join WiFi yang sama dengan iPhone, isi IP device (tampil di layar Web Server device)", systemImage: "2.circle")
                Label("Live/Sesi butuh salah satu di atas; Navigasi cukup BLE saja", systemImage: "3.circle")
            }
            .font(.footnote)

            Section("Pilihan Rute (Navigasi)") {
                Toggle("Hindari jalan tol", isOn: $route.avoidTolls)
                Toggle("Hindari jalan raya", isOn: $route.avoidHighways)
                Label("Diterapkan saat menghitung rute, seperti di Google Maps.", systemImage: "signpost.right.and.left")
                    .font(.footnote)
                    .foregroundColor(.secondary)
            }

            Section("Bluetooth (Navigasi)") {
                Label(ble.status.text, systemImage: "dot.radiowaves.left.and.right")
                HStack {
                    Button("Scan / Ulangi") { ble.scan() }
                        .buttonStyle(.borderedProminent)
                    Button("Putus") { ble.disconnect() }
                        .buttonStyle(.bordered)
                        .disabled(!ble.status.isConnected)
                }
            }

            Section("Uji Manual BLE") {
                ManualTestSection()
            }

            Section("Log BLE (RX)") {
                if ble.rxLog.isEmpty {
                    Text("Belum ada data masuk")
                        .foregroundColor(.secondary)
                } else {
                    Text(ble.rxLog.suffix(600))
                        .font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled)
                }
                Button("Bersihkan Log", role: .destructive) { ble.clearLog() }
            }

            Section("Tentang") {
                LabeledContent("Aplikasi", value: "MuchRacing iOS 1.0")
                LabeledContent("Firmware", value: "v4.1.4")
                LabeledContent("Protokol", value: "BLE NUS + HTTP AP")
                Label("Nav JSON: {icon,dist,text} • LogPacket 32B di SD", systemImage: "info.circle")
            }
            .font(.footnote)
        }
        .navigationTitle("Setelan")
        .tint(.neonOrange)
        .scrollContentBackground(.hidden)
        .background(RacingBackground())
        .onAppear {
            ipInput = APClient.savedBaseURL()
        }
    }

    private var locationDetail: String {
        switch location.authorization {
        case .notDetermined: return "Belum diatur — untuk peta & rute navigasi."
        case .authorizedWhenInUse, .authorizedAlways: return "Diizinkan. GPS device berfungsi."
        case .denied: return "Diblokir — pelacakan GPS & rute tidak jalan."
        case .restricted: return "Diblokir oleh pengaturan orang tua/administrasi."
        default: return "Memperbarui status..."
        }
    }

    private var bluetoothDetail: String {
        switch ble.centralState {
        case .poweredOn: return "Aktif. Siap scan MuchRacing-Nav."
        case .poweredOff: return "Bluetooth iPhone mati — aktifkan dari Control Center."
        case .unauthorized: return "Izin diblokir — izinkan di Pengaturan > Bluetooth."
        case .unsupported: return "Perangkat tidak mendukung Bluetooth LE."
        default: return "Memperbarui status..."
        }
    }

    private func locationAction() {
        if location.authorization == .notDetermined {
            location.requestPermission()
        } else if location.isBlocked {
            openSystemSettings()
        }
    }
}

// MARK: - Baris status izin akses

private struct PermissionRow: View {
    let title: String
    let detail: String
    let dot: Color
    var actionTitle: String?
    var action: (() -> Void)? = nil

    var body: some View {
        HStack(spacing: 12) {
            Circle()
                .fill(dot)
                .frame(width: 10, height: 10)
                .neonGlow(dot, radius: 5)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.subheadline.weight(.semibold))
                Text(detail)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            Spacer()
            if let actionTitle {
                Button(actionTitle, action: action ?? {})
                    .buttonStyle(.bordered)
                    .controlSize(.small)
            }
        }
        .padding(.vertical, 4)
    }
}