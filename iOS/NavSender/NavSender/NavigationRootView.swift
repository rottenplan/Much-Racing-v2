import SwiftUI
import MapKit
import CoreLocation

// MARK: - Navigasi ala Google Maps: pilih tujuan di peta lalu kirim turn-by-turn
//
// Layar utama dibangun dari peta penuh:
//  • Ketuk di mana saja di peta -> pin tujuan dipasang/digerakkan.
//  • Cari tempat/alamat di kolom atas -> daftar hasil -> ketuk untuk memilih.
//  • Kartu bawah menampilkan pin terpilih + tombol "Rute ke sini", lalu
//    "Mulai Navigasi" setelah rute dihitung (dikirim lewat BLE).

struct NavigationRootView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var route: RouteManager
    @EnvironmentObject var location: LocationTracker
    @EnvironmentObject var live: LiveStore
    @EnvironmentObject var navSession: NavigationSession

    @State private var query = ""
    @State private var searchResults: [MKMapItem] = []
    @State private var searching = false
    @State private var destination: MKMapItem?

    @State private var camera: MapCameraPosition = .userLocation(
        fallback: .region(MKCoordinateRegion(
            center: CLLocationCoordinate2D(latitude: -6.2, longitude: 106.8),
            span: MKCoordinateSpan(latitudeDelta: 0.05, longitudeDelta: 0.05))))
    @FocusState private var searchFocused: Bool

    @State private var showNavSession = false
    @State private var showError = false
    @State private var errorText = ""

    var body: some View {
        ZStack(alignment: .top) {
            mapLayer
                .ignoresSafeArea(edges: .bottom)

            VStack(spacing: 8) {
                searchBar
                if searching {
                    ProgressView().tint(.neonOrange)
                }
                if !searchResults.isEmpty {
                    resultList
                }
                if location.isBlocked {
                    permissionBanner
                }
            }
            .padding(.horizontal, 12)
            .padding(.top, 4)

            VStack {
                Spacer()
                bottomCard
            }
            .padding(12)
        }
        .background(RacingBackground())
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

    // MARK: - Peta

    private var mapLayer: some View {
        MapReader { proxy in
            Map(position: $camera) {
                if route.routes.count > 1 {
                    // Rute alternatif: semua digambar, terpilih disorot.
                    ForEach(Array(route.routes.enumerated()), id: \.offset) { index, r in
                        let isSelected = index == route.selectedRouteIndex
                        MapPolyline(r.polyline)
                            .stroke(isSelected ? .neonCyan : Color.gray.opacity(0.5),
                                    style: StrokeStyle(lineWidth: isSelected ? 8 : 4,
                                                       lineCap: .round, lineJoin: .round))
                    }
                } else if let poly = route.route?.polyline {
                    MapPolyline(poly)
                        .stroke(.white, style: StrokeStyle(lineWidth: 8, lineCap: .round, lineJoin: .round))
                    MapPolyline(poly)
                        .stroke(.blue, style: StrokeStyle(lineWidth: 5, lineCap: .round, lineJoin: .round))
                }
                if let dest = destination {
                    Marker("Tujuan", systemImage: "flag.checkered", coordinate: dest.placemark.coordinate)
                        .tint(.red)
                }
                UserAnnotation()
            }
            .mapStyle(.standard(elevation: .flat, emphasis: .muted))
            .mapControls {
                MapCompass()
            }
            // Ketuk peta untuk menandai / memindahkan tujuan.
            .gesture(
                SpatialTapGesture(count: 1, coordinateSpace: .local)
                    .onEnded { value in
                        if let coord = proxy.convert(value.location, from: .local) {
                            setDestination(at: coord)
                        }
                    }
            )
        }
    }

    // MARK: - Pencarian

    private var searchBar: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass")
                .foregroundColor(.secondary)
            TextField("Cari tempat / alamat", text: $query)
                .textInputAutocapitalization(.words)
                .disableAutocorrection(false)
                .submitLabel(.search)
                .focused($searchFocused)
                .onSubmit { search() }

            if !query.isEmpty {
                Button {
                    clearSearch()
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundColor(.secondary)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .fill(RacingTheme.card)
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(RacingTheme.cardBorder, lineWidth: 1))
        )
        .shadow(color: .black.opacity(0.4), radius: 6, y: 2)
    }

    @MainActor
    private func search() {
        let q = query.trimmingCharacters(in: .whitespaces)
        clearSearch()
        guard !q.isEmpty else { return }

        var center = location.lastLocation?.coordinate
        if center == nil, let region = camera.region {
            center = region.center
        }
        guard let center else {
            errorText = "Lokasi belum tersedia. Setujui izin lokasi, lalu coba lagi."
            showError = true
            return
        }

        searching = true
        Task {
            defer { searching = false }
            do {
                let items = try await route.searchPlaces(q, near: center)
                searchResults = Array(items.prefix(8))
            } catch {
                searchResults = []
            }
        }
    }

    @ViewBuilder
    private var permissionBanner: some View {
        HStack(spacing: 10) {
            Image(systemName: "nosign")
                .font(.title3)
                .foregroundColor(.neonRed)
                .neonGlow(.neonRed, radius: 4)
            Text(location.authorization == .restricted
                 ? "Akses lokasi dibatasi oleh pengaturan perangkat."
                 : "Akses lokasi diblokir. Navigasi butuh posisi GPS.")
                .font(.footnote.weight(.medium))
                .foregroundColor(.white)
                .lineLimit(2)
            Spacer(minLength: 4)
            Button("Buka Pengaturan") { openSystemSettings() }
                .font(.caption.weight(.bold))
                .buttonStyle(.borderedProminent)
                .controlSize(.small)
                .tint(.neonRed)
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .fill(Color.neonRed.opacity(0.16))
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color.neonRed.opacity(0.6), lineWidth: 1))
        )
        .transition(.move(edge: .top).combined(with: .opacity))
        .animation(.spring(response: 0.35, dampingFraction: 0.8), value: location.isBlocked)
    }

    @ViewBuilder
    private var resultList: some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                ForEach(Array(searchResults.enumerated()), id: \.offset) { _, item in
                    Button {
                        select(item)
                    } label: {
                        HStack(spacing: 10) {
                            Image(systemName: "mappin.circle.fill")
                                .foregroundColor(.neonOrange)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(item.name ?? "Tanpa nama")
                                    .font(.subheadline.weight(.semibold))
                                    .foregroundColor(.white)
                                Text(subtitle(item))
                                    .font(.caption2)
                                    .foregroundColor(.secondary)
                                    .lineLimit(1)
                            }
                            Spacer()
                        }
                        .padding(.vertical, 8)
                        .padding(.horizontal, 10)
                        .overlay(alignment: .bottom) {
                            Divider().overlay(Color.neonOrange.opacity(0.12))
                        }
                    }
                }
            }
            .background(RacingTheme.card)
            .clipShape(RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(RacingTheme.cardBorder, lineWidth: 1))
        }
        .frame(maxHeight: 300)
    }

    private func subtitle(_ item: MKMapItem) -> String {
        if let s = item.placemark.subtitle, !s.isEmpty { return s }
        let parts = [item.placemark.locality, item.placemark.administrativeArea].compactMap { $0 }
        return parts.joined(separator: ", ")
    }

    // MARK: - Pemilihan tujuan

    private func select(_ item: MKMapItem) {
        destination = item
        clearSearch()
        moveCamera(to: item.placemark.coordinate)
    }

    private func setDestination(at coord: CLLocationCoordinate2D) {
        searchFocused = false
        clearSearch()

        let placemark = MKPlacemark(coordinate: coord)
        let item = MKMapItem(placemark: placemark)
        item.name = String(format: "%.5f, %.5f", coord.latitude, coord.longitude)
        withAnimation(.easeInOut(duration: 0.2)) {
            destination = item
        }
        moveCamera(to: coord)

        // Best-effort: ganti nama dengan alamat sesungguhnya.
        Task {
            guard let place = try? await CLGeocoder()
                .reverseGeocodeLocation(CLLocation(latitude: coord.latitude,
                                                   longitude: coord.longitude))
                .first else { return }
            let parts = [place.name, place.thoroughfare, place.locality]
                .compactMap { $0 }
            guard let first = parts.first else { return }
            destination?.name = first
        }
    }

    private func moveCamera(to coord: CLLocationCoordinate2D) {
        withAnimation(.easeInOut(duration: 0.35)) {
            camera = .region(MKCoordinateRegion(center: coord,
                                                span: MKCoordinateSpan(latitudeDelta: 0.006,
                                                                       longitudeDelta: 0.006)))
        }
    }

    private func clearSearch() {
        query = ""
        searchResults = []
        searching = false
    }

    @MainActor
    private func computeRouteToDestination() {
        guard let destination else { return }

        guard ble.status.isConnected else {
            errorText = "Hubungkan dulu ke MuchRacing-Nav dengan tombol Scan."
            showError = true
            return
        }

        if location.authorization == .notDetermined {
            location.requestPermission()
            errorText = "Setujui izin lokasi yang muncul, lalu tekan 'Rute ke sini' lagi."
            showError = true
            return
        }

        if location.isBlocked {
            openSystemSettings()
            errorText = "Akses lokasi diblokir. Buka Pengaturan lalu izinkan posisi GPS."
            showError = true
            return
        }

        route.isComputing = true
        Task {
            defer { route.isComputing = false }
            do {
                let ok = try await route.computeRoute(from: MKMapItem.forCurrentLocation(), to: destination)
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

    // MARK: - Kartu bawah

    private var bottomCard: some View {
        let accent: Color
        if !route.steps.isEmpty {
            accent = .neonGreen
        } else if destination != nil {
            accent = .neonCyan
        } else {
            accent = .neonOrange
        }

        return RacingCard(accent: accent, glow: true) {
            VStack(spacing: 10) {
                statusRow

                Divider().overlay(Color.neonOrange.opacity(0.2))

                if route.steps.isEmpty {
                    destinationArea
                } else {
                    routeArea
                }

                if live.connected && route.steps.isEmpty {
                    Label("Catatan: hitung rute butuh internet. Keluar dulu dari AP MuchRacing-GPS.", systemImage: "info.circle")
                        .font(.footnote)
                        .foregroundColor(.neonYellow)
                }
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var statusRow: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(ble.status.isConnected ? Color.neonGreen : Color.neonRed)
                .frame(width: 9, height: 9)
                .neonGlow(ble.status.isConnected ? .neonGreen : .neonRed, radius: 4)
            Text(ble.status.text)
                .font(.caption.weight(.semibold))
                .lineLimit(1)
            Spacer()

            Button {
                camera = .userLocation(fallback: .automatic)
            } label: {
                Image(systemName: "location.fill")
                    .font(.footnote.weight(.semibold))
                    .foregroundColor(.neonCyan)
                    .neonGlow(.neonCyan, radius: 4)
            }
            .buttonStyle(.plain)
            .padding(.trailing, 4)

            if ble.status.isConnected {
                Button("Putus") { ble.disconnect() }
                    .font(.caption.weight(.bold))
                    .buttonStyle(.bordered)
                    .controlSize(.small)
            } else {
                Button("Scan / Ulangi") { ble.scan() }
                    .font(.caption.weight(.bold))
                    .buttonStyle(.borderedProminent)
                    .controlSize(.small)
            }
        }
    }

    @ViewBuilder
    private var destinationArea: some View {
        if let dest = destination {
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 8) {
                    Image(systemName: "flag.checkered")
                        .foregroundColor(.neonRed)
                        .neonGlow(.neonRed, radius: 4)
                    Text(dest.name ?? "Tujuan")
                        .font(.subheadline.weight(.bold))
                        .foregroundColor(.white)
                        .lineLimit(2)
                    Spacer()
                    Button {
                        withAnimation(.easeInOut(duration: 0.2)) { destination = nil }
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundColor(.secondary)
                    }
                    .buttonStyle(.plain)
                }
                Text(String(format: "%.5f, %.5f", dest.placemark.coordinate.latitude,
                            dest.placemark.coordinate.longitude))
                    .font(.caption2.monospaced())
                    .foregroundColor(.secondary)

                Divider().overlay(Color.neonOrange.opacity(0.2))
                RouteOptionsView()

                HStack(spacing: 12) {
                    Button {
                        computeRouteToDestination()
                    } label: {
                        if route.isComputing {
                            ProgressView().frame(maxWidth: .infinity)
                        } else {
                            Label("Rute ke sini", systemImage: "point.topleft.down.curvedto.point.bottomright.up")
                                .frame(maxWidth: .infinity)
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.neonCyan)
                    .disabled(!ble.status.isConnected || route.isComputing)

                    Button {
                        setDestination(at: location.lastLocation?.coordinate ?? dest.placemark.coordinate)
                    } label: {
                        Image(systemName: "paperplane.fill")
                    }
                    .buttonStyle(.bordered)
                    .disabled(route.isComputing)
                }
            }
        } else {
            HStack(spacing: 10) {
                Image(systemName: "hand.tap.fill")
                    .font(.title3)
                    .foregroundColor(.neonOrange)
                    .neonGlow(.neonOrange, radius: 4)
                Text("**Ketuk peta** untuk menandai tujuan, atau cari tempat di kolom atas.")
                    .font(.footnote)
                    .foregroundColor(.secondary)
            }
            .padding(.vertical, 4)
        }
    }

    private var routeArea: some View {
        VStack(alignment: .leading, spacing: 8) {
            if route.routes.count > 1 {
                RouteAlternatives {
                    if navSession.isActive { navSession.start() }
                }
            }

            Text(route.routeSummary)
                .font(.headline.weight(.bold))
                .fontDesign(.rounded)
                .monospacedDigit()
            Text("\(route.steps.count) langkah • mulai: \(route.steps.first?.instruction ?? "")")
                .font(.caption)
                .foregroundColor(.secondary)

            HStack(spacing: 12) {
                Button {
                    showNavSession = true
                } label: {
                    Label("Mulai Navigasi", systemImage: "play.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.neonGreen)
                .disabled(!ble.status.isConnected)

                Button {
                    sessionResetAndClear()
                } label: {
                    Label("Hapus Rute", systemImage: "trash")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .tint(.neonRed)
            }
        }
    }

    private func sessionResetAndClear() {
        navSession.reset()
        route.reset()
    }
}