import SwiftUI
import MapKit

// MARK: - Navigasi: cari tujuan + rute, lalu kirim turn-by-turn ke device via BLE

struct NavigationRootView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var route: RouteManager
    @EnvironmentObject var location: LocationTracker
    @EnvironmentObject var live: LiveStore

    @State private var destinationQuery = ""
    @State private var showNavSession = false
    @State private var showError = false
    @State private var errorText = ""

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                statusCard
                connectionButtons
                if live.connected {
                    Label("Catatan: rute perlu internet. Keluar dari AP MuchRacing-GPS untuk menghitung rute.", systemImage: "info.circle")
                        .font(.footnote)
                        .foregroundColor(.yellow)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                destinationSection
                if route.isComputing {
                    ProgressView("Menghitung rute...")
                }
                routeResultCard
            }
            .padding()
        }
        .navigationTitle("Navigasi")
        .navigationDestination(isPresented: $showNavSession) {
            NavigationSessionView()
        }
        .alert("Perhatian", isPresented: $showError) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(errorText)
        }
    }

    private var statusCard: some View {
        HStack(spacing: 12) {
            Circle()
                .fill(Color(red: ble.status.color.0, green: ble.status.color.1, blue: ble.status.color.2))
                .frame(width: 14, height: 14)
            VStack(alignment: .leading, spacing: 2) {
                Text(ble.status.text)
                    .font(.subheadline)
                    .lineLimit(2)
                Text("Kirim manuver via BLE MuchRacing-Nav")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            Spacer()
        }
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color(.secondarySystemBackground)))
    }

    private var connectionButtons: some View {
        HStack(spacing: 12) {
            Button {
                ble.scan()
            } label: {
                Label("Scan / Ulangi", systemImage: "magnifyingglass")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)

            Button {
                ble.disconnect()
            } label: {
                Label("Putus", systemImage: "xmark.circle")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(!ble.status.isConnected)
        }
    }

    private var destinationSection: some View {
        VStack(spacing: 8) {
            TextField("Tujuan (alamat / nama tempat)", text: $destinationQuery)
                .textFieldStyle(.roundedBorder)
                .submitLabel(.search)
                .onSubmit { startNavigation() }

            Button {
                startNavigation()
            } label: {
                Label("Cari & Hitung Rute", systemImage: "point.topleft.down.curvedto.point.bottomright.up")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .disabled(destinationQuery.trimmingCharacters(in: .whitespaces).isEmpty)
        }
    }

    private var routeResultCard: some View {
        Group {
            if route.steps.isEmpty {
                EmptyView()
            } else {
                VStack(alignment: .leading, spacing: 8) {
                    Text(route.routeSummary)
                        .font(.headline)
                    Text("\(route.steps.count) langkah • mulai: \(route.steps.first?.instruction ?? "")")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Button {
                        showNavSession = true
                    } label: {
                        Label("Mulai Navigasi", systemImage: "play.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.green)
                    .disabled(!ble.status.isConnected)
                }
                .padding(12)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color(.secondarySystemBackground)))
            }
        }
    }

    @MainActor
    private func startNavigation() {
        let query = destinationQuery.trimmingCharacters(in: .whitespaces)
        guard !query.isEmpty else { return }

        guard ble.status.isConnected else {
            errorText = "Hubungkan dulu ke MuchRacing-Nav dengan tombol Scan."
            showError = true
            return
        }

        if location.authorization == .notDetermined {
            location.requestPermission()
            errorText = "Setujui izin lokasi yang muncul, lalu ketuk 'Cari & Hitung Rute' lagi."
            showError = true
            return
        }

        guard let userLoc = location.lastLocation else {
            errorText = "Lokasi iPhone belum tersedia. Aktifkan izin lokasi (Settings > Privacy > Location Services), lalu coba lagi."
            showError = true
            return
        }

        route.isComputing = true
        Task {
            defer { route.isComputing = false }
            do {
                guard let dest = try await route.searchDestination(query, near: userLoc.coordinate) else {
                    errorText = "Tempat tidak ditemukan. Coba nama atau alamat lain."
                    showError = true
                    return
                }
                let ok = try await route.computeRoute(from: MKMapItem.forCurrentLocation(), to: dest)
                if !ok {
                    errorText = "Rute tidak ditemukan ke tujuan tersebut."
                    showError = true
                } else {
                    showNavSession = true
                }
            } catch {
                errorText = "Gagal menghitung rute: \(error.localizedDescription)"
                showError = true
            }
        }
    }
}