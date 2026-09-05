import SwiftUI

// MARK: - Beranda: hub dashboard + status device (mirip PRO HUB web)

struct HomeView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var live: LiveStore
    @Binding var selection: AppTab

    private let columns = [GridItem(.flexible()), GridItem(.flexible())]

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                header
                statusRow
                hubGrid
                hintCard
            }
            .padding()
        }
        .background(colorBackground.ignoresSafeArea())
        .navigationTitle("MuchRacing")
        .onAppear { ble.start() }
    }

    private var colorBackground: Color {
        Color(red: 0.06, green: 0.065, blue: 0.075)
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("PRO HUB")
                .font(.caption).bold()
                .foregroundColor(.orange)
            Text("Kontrol penuh device dari iPhone")
                .font(.title2).bold()
                .foregroundColor(.white)
            Text("Telemetri live, navigasi, drag, dan sesi — semua langsung dari perangkat MuchRacing.")
                .font(.footnote)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var statusRow: some View {
        HStack(spacing: 12) {
            statusCard(title: "Bluetooth (Nav)",
                       dot: ble.status.isConnected ? .green : .gray,
                       text: ble.status.text,
                       action: { selection = .nav })
            statusCard(title: "WiFi (Live/Sesi)",
                       dot: live.connected ? .green : .gray,
                       text: live.connected ? "Device terhubung" : "Luar jaringan AP",
                       action: { selection = .settings })
        }
    }

    private func statusCard(title: String, dot: Color, text: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 6) {
                    Circle().fill(dot).frame(width: 8, height: 8)
                    Text(title).font(.caption).bold().foregroundColor(.secondary)
                }
                Text(text)
                    .font(.footnote)
                    .foregroundColor(.white)
                    .lineLimit(2)
                Spacer(minLength: 0)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(12)
            .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
        }
        .buttonStyle(.plain)
    }

    private var hubGrid: some View {
        LazyVGrid(columns: columns, spacing: 12) {
            hubTile("Live Telemetri", "gauge.with.dots.needle.67percent", "Speed, RPM, trip, bat", color: .green) { selection = .live }
            hubTile("GPS Navigasi", "location.north.line.fill", "Turn-by-turn via BLE", color: .orange) { selection = .nav }
            hubTile("Drag Meter", "flag.checkered", "0-60, 100, 402 m", color: .red) { selection = .live }
            hubTile("Sesi & Lap", "clock.badge.checkmark", "Lap times dari SD", color: .cyan) { selection = .sessions }
            hubTile("WiFi Device", "wifi", "AP/hotspot, IP", color: .blue) { selection = .settings }
            hubTile("Uji Manual BLE", "dot.radiowaves.left.and.right", "Kirim manuver", color: .purple) { selection = .settings }
        }
    }

    private func hubTile(_ title: String, _ icon: String, _ desc: String, color: Color, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 8) {
                Image(systemName: icon)
                    .font(.title2)
                    .foregroundColor(color)
                Text(title)
                    .font(.subheadline).bold()
                    .foregroundColor(.white)
                    .multilineTextAlignment(.leading)
                Text(desc)
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.leading)
                Spacer(minLength: 0)
            }
            .frame(maxWidth: .infinity, minHeight: 110, alignment: .leading)
            .padding(12)
            .background(RoundedRectangle(cornerRadius: 12)
                .fill(Color(.secondarySystemBackground))
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(color.opacity(0.35), lineWidth: 1)))
        }
        .buttonStyle(.plain)
    }

    private var hintCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label("Cara pakai", systemImage: "lightbulb.fill")
                .font(.headline)
                .foregroundColor(.yellow)
            Text("1. Nyalakan WiFi AP device (SSID \u{201C}MuchRacing-GPS\u{201D}, sandi 12345678).\n2. Di iPhone: Settings \u{203A} Wi-Fi \u{203A} pilih MuchRacing-GPS.\n3. Kembali ke app — Live, Drag, dan Sesi otomatis terhubung.\n4. Untuk Navigasi, gunakan BLE MuchRacing-Nav saat tidak di AP.")
                .font(.footnote)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
    }
}