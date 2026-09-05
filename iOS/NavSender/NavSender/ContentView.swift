import SwiftUI
import MapKit

struct ContentView: View {
    @StateObject private var ble = BLEManager()
    @StateObject private var route = RouteManager()
    @StateObject private var location = LocationTracker()

    @State private var destinationQuery = ""
    @State private var showNavSession = false
    @State private var showError = false
    @State private var errorText = ""

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    statusCard
                    connectionButtons
                    destinationSection
                    if route.isComputing {
                        ProgressView("Menghitung rute...")
                    }
                    routeResultCard
                    ManualTestSection()
                }
                .padding()
            }
            .navigationTitle("MuchRacing Nav")
            .navigationDestination(isPresented: $showNavSession) {
                NavigationSessionView()
            }
            .alert("Perhatian", isPresented: $showError) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(errorText)
            }
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        ble.clearLog()
                    } label: {
                        Image(systemName: "trash")
                    }
                }
            }
        }
        .environmentObject(ble)
        .environmentObject(route)
        .environmentObject(location)
        .onAppear { ble.start() }
    }

    // MARK: - Status & koneksi

    private var statusCard: some View {
        HStack(spacing: 12) {
            Circle()
                .fill(Color(red: ble.status.color.0, green: ble.status.color.1, blue: ble.status.color.2))
                .frame(width: 14, height: 14)
            Text(ble.status.text)
                .font(.subheadline)
                .lineLimit(2)
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

    // MARK: - Tujuan & rute

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

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}