import Foundation
import MapKit

// Satu langkah navigasi yang siap dikirim ke device.
// icon mengikuti kode firmware: 0=tiba, 1=lurus, 2=kiri sedikit, 3=kiri,
// 4=kiri tajam, 5=kanan sedikit, 6=kanan, 7=kanan tajam, 8=putar balik, 9=bundaran.
struct NavStep: Identifiable {
    let id: Int                          // index langkah di rute
    let icon: Int
    let distanceM: Int                   // jarak dari titik manuver sebelumnya
    let instruction: String
    let coordinate: CLLocationCoordinate2D // titik manuver (akhir langkah ini)

    init(id: Int, icon: Int, distanceM: Int, instruction: String,
         coordinate: CLLocationCoordinate2D) {
        self.id = id
        self.icon = icon
        self.distanceM = distanceM
        self.instruction = instruction
        self.coordinate = coordinate
    }
}

// MARK: - Snapshot rute tersimpan (untuk navigasi offline lewat BLE saja)
//
// Rute dihitung saat ada internet, lalu disimpan di UserDefaults agar bisa
// dipakai lagi tanpa internet: langkah + garis rute + ringkasan cukup untuk
// turn-by-turn via BLE (GPS iPhone tetap jalan tanpa jaringan apa pun).

private struct StorableCoord: Codable {
    let lat: Double
    let lon: Double
}

private struct StorableStep: Codable {
    let id: Int
    let icon: Int
    let distanceM: Int
    let instruction: String
    let lat: Double
    let lon: Double

    var navStep: NavStep {
        NavStep(id: id, icon: icon, distanceM: distanceM,
                instruction: instruction,
                coordinate: CLLocationCoordinate2D(latitude: lat, longitude: lon))
    }

    init(step: NavStep) {
        id = step.id
        icon = step.icon
        distanceM = step.distanceM
        instruction = step.instruction
        lat = step.coordinate.latitude
        lon = step.coordinate.longitude
    }
}

private struct SavedRoute: Codable {
    let steps: [StorableStep]
    let polyline: [StorableCoord]
    let summary: String
    let distanceMeters: Double
    let travelSeconds: TimeInterval
    let destinationName: String
}

@MainActor
final class RouteManager: ObservableObject {

    @Published var isComputing = false
    @Published var errorMessage: String?
    @Published var route: MKRoute?
    @Published var routeSummary = ""
    @Published var steps: [NavStep] = []
    // Seluruh rute hasil perhitungan (rute alternatif ala Google Maps).
    // Format 0 adalah rute bawaan; pengendara bisa memilih yang lain.
    @Published var routes: [MKRoute] = []
    @Published var selectedRouteIndex = 0

    // Ringkasan rute aktif tanpa bergantung pada objek MKRoute yang hidup di
    // memori. Diisi saat menghitung rute dan saat memuat rute tersimpan, supaya
    // navigasi (ETA) & gambar peta tetap bisa berfungsi tanpa internet.
    @Published var routePolyline: MKPolyline?
    @Published var distanceMeters: Double = 0
    @Published var travelSeconds: TimeInterval = 0
    // true bila rute sedang dipakai berasal dari snapshot tersimpan (offline).
    @Published var isSavedRoute = false
    @Published var savedDestinationText: String?

    private static let savedRouteKey = "route.saved"

    // Preferensi rute ala Google Maps: otomatis disimpan (UserDefaults) agar
    // konsisten di semua tempat yang menghitung rute (Navigasi, Live, Setelan).
    @Published var avoidTolls = UserDefaults.standard.bool(forKey: "route.avoidTolls") {
        didSet { UserDefaults.standard.set(avoidTolls, forKey: "route.avoidTolls") }
    }
    @Published var avoidHighways = UserDefaults.standard.bool(forKey: "route.avoidHighways") {
        didSet { UserDefaults.standard.set(avoidHighways, forKey: "route.avoidHighways") }
    }

    func reset() {
        route = nil
        routeSummary = ""
        steps = []
        routes = []
        selectedRouteIndex = 0
        routePolyline = nil
        distanceMeters = 0
        travelSeconds = 0
        isSavedRoute = false
        savedDestinationText = nil
        errorMessage = nil
        isComputing = false
        UserDefaults.standard.removeObject(forKey: Self.savedRouteKey)
    }

    // Pilih salah satu rute alternatif; mengisi route/steps/summary aktif.
    func selectRoute(at index: Int) {
        guard index >= 0, index < routes.count else { return }
        selectedRouteIndex = index
        let chosen = routes[index]
        route = chosen
        steps = buildSteps(from: chosen)
        routeSummary = summary(for: chosen)
        routePolyline = chosen.polyline
        distanceMeters = chosen.distance
        travelSeconds = chosen.expectedTravelTime
        isSavedRoute = false
        savedDestinationText = nil
        saveForOffline()
    }

    // Cari tempat tujuan lewat pencarian lokal MapKit (tanpa API key).
    func searchDestination(_ query: String,
                           near center: CLLocationCoordinate2D) async throws -> MKMapItem? {
        try await searchPlaces(query, near: center).first
    }

    // Daftar hasil pencarian tempat (untuk picker ala Google Maps).
    func searchPlaces(_ query: String,
                      near center: CLLocationCoordinate2D) async throws -> [MKMapItem] {
        let request = MKLocalSearch.Request()
        request.naturalLanguageQuery = query
        request.region = MKCoordinateRegion(center: center,
                                            latitudinalMeters: 40_000,
                                            longitudinalMeters: 40_000)
        let search = MKLocalSearch(request: request)

        return try await withCheckedThrowingContinuation { continuation in
            search.start { response, error in
                if let error {
                    continuation.resume(throwing: error)
                } else {
                    continuation.resume(returning: response?.mapItems ?? [])
                }
            }
        }
    }

    // Hitung rute berkendara dari posisi iPhone ke tujuan.
    @discardableResult
    func computeRoute(from fromItem: MKMapItem, to toItem: MKMapItem) async throws -> Bool {
        let request = MKDirections.Request()
        request.source = fromItem
        request.destination = toItem
        request.transportType = .automobile
        // Minta rute alternatif supaya pengendara bisa membandingkan (ala Google Maps).
        request.requestsAlternateRoutes = true

        // Terapkan preferensi "hindari" ala Google Maps (MapKit: iOS 16+).
        request.tollPreference = avoidTolls ? .avoid : .any
        request.highwayPreference = avoidHighways ? .avoid : .any

        let directions = MKDirections(request: request)
        let response: MKDirections.Response =
            try await withCheckedThrowingContinuation { continuation in
                directions.calculate { resp, error in
                    if let error {
                        continuation.resume(throwing: error)
                    } else if let resp {
                        continuation.resume(returning: resp)
                    } else {
                        continuation.resume(throwing: NSError(domain: "RouteManager", code: -1))
                    }
                }
            }

        guard !response.routes.isEmpty else { return false }

        routes = response.routes
        selectedRouteIndex = 0
        savedDestinationText = toItem.name ?? "Tujuan"
        selectRoute(at: 0)
        return true
    }

    // MARK: - Simpan / pulihkan rute untuk navigasi offline (BLE saja)

    // Gambar garis rute di peta: preferensi polyline tersimpan (offline),
    // kalau belum ada pakai polyline dari MKRoute yang masih aktif.
    var polylineForDrawing: MKPolyline? { routePolyline ?? route?.polyline }

    func hasSavedRoute() -> Bool {
        UserDefaults.standard.data(forKey: Self.savedRouteKey) != nil
    }

    // Simpan snapshot rute yang baru dihitung (saat masih online) supaya bisa
    // dikembalikan tanpa internet kapan pun — mis. setelah app di-restart.
    private func saveForOffline() {
        guard let polyline = routePolyline, !steps.isEmpty else { return }
        var coords: [StorableCoord] = []
        let points = polyline.points()
        for i in 0..<polyline.pointCount {
            let c = points[i].coordinate
            coords.append(StorableCoord(lat: c.latitude, lon: c.longitude))
        }
        let snapshot = SavedRoute(steps: steps.map(StorableStep.init),
                                  polyline: coords,
                                  summary: routeSummary,
                                  distanceMeters: distanceMeters,
                                  travelSeconds: travelSeconds,
                                  destinationName: savedDestinationText ?? "Tujuan")
        if let data = try? JSONEncoder().encode(snapshot) {
            UserDefaults.standard.set(data, forKey: Self.savedRouteKey)
        }
    }

    // Muat rute tersimpan (dipanggil saat app dibuka). Tidak menimpa rute yang
    // masih aktif di memori kalau memang sudah ada.
    func restoreSavedRoute() {
        guard steps.isEmpty else { return }
        guard let data = UserDefaults.standard.data(forKey: Self.savedRouteKey),
              let snapshot = try? JSONDecoder().decode(SavedRoute.self, from: data),
              !snapshot.steps.isEmpty else { return }

        steps = snapshot.steps.map(\.navStep)
        routeSummary = snapshot.summary
        distanceMeters = snapshot.distanceMeters
        travelSeconds = snapshot.travelSeconds
        savedDestinationText = snapshot.destinationName
        isSavedRoute = true

        route = nil
        routes = []
        selectedRouteIndex = 0
        let coords = snapshot.polyline.map {
            CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon)
        }
        routePolyline = coords.isEmpty ? nil : MKPolyline(coordinates: coords, count: coords.count)
    }

    // Pesan error yang ramah kalau internet sedang tidak tersedia (rute dihitung
    // lewat server Apple Maps). Internet hanya dibutuhkan saat menghitung rute;
    // navigasinya sendiri berjalan offline via BLE + GPS iPhone.
    func friendlyError(_ error: Error) -> String {
        let ns = error as NSError
        if ns.domain == NSURLErrorDomain {
            switch ns.code {
            case NSURLErrorNotConnectedToInternet,
                 NSURLErrorInternationalRoamingOff,
                 NSURLErrorCannotFindHost,
                 NSURLErrorCannotConnectToHost,
                 NSURLErrorNetworkConnectionLost,
                 NSURLErrorTimedOut:
                return "Tidak ada koneksi internet. Hitung rute dulu saat online "
                    + "(WiFi/cellular, bukan AP MuchRacing-GPS), lalu simpan & "
                    + "navigasi tetap bisa dipakai lewat BLE tanpa internet."
            default: break
            }
        }
        return "Gagal menghitung rute: \(error.localizedDescription)"
    }

    // MARK: - Konversi langkah MapKit -> NavStep dengan icon manuver

    private func summary(for r: MKRoute) -> String {
        String(format: "%.1f km • %d menit",
               r.distance / 1000.0,
               Int(r.expectedTravelTime / 60))
    }

    private func buildSteps(from route: MKRoute) -> [NavStep] {
        let raw = route.steps
        var result: [NavStep] = []

        // Langkah pertama biasanya "Start out on ..." (titik berangkat) — dilewati.
        let firstIdx = raw.count > 1 ? 1 : 0
        guard firstIdx < raw.count else { return result }

        for i in firstIdx..<raw.count {
            let step = raw[i]
            let isLast = (i == raw.count - 1)

            let icon: Int
            var text: String
            if isLast {
                icon = 0 // tiba
                text = "You have arrived"
            } else {
                // Arah masuk vs arah keluar titik manuver -> sudut belokan.
                let inHeading = heading(of: step.polyline)
                let nextHeading = heading(of: raw[i + 1].polyline)
                var delta = nextHeading - inHeading
                delta = (delta + 540).truncatingRemainder(dividingBy: 360) - 180
                icon = classify(delta: delta)
                text = step.instructions.isEmpty ? "Continue" : step.instructions
            }

            result.append(NavStep(id: i,
                                  icon: icon,
                                  distanceM: isLast ? 0 : Int(round(step.distance)),
                                  instruction: text,
                                  coordinate: endCoordinate(of: step.polyline)))
        }
        return result
    }

    // Klasifikasi sudut belokan (derajat, -180..180) menjadi kode icon firmware.
    private func classify(delta: Double) -> Int {
        if delta >= 140 || delta <= -140 { return 8 } // putar balik
        if delta <= -100 { return 4 }                 // kiri tajam
        if delta <= -45  { return 3 }                 // kiri
        if delta <= -20  { return 2 }                 // kiri sedikit
        if delta >= 100  { return 7 }                 // kanan tajam
        if delta >= 45   { return 6 }                 // kanan
        if delta >= 20   { return 5 }                 // kanan sedikit
        return 1                                      // lurus
    }

    // Arah (bearing) dari titik pertama ke titik terakhir polyline.
    private func heading(of polyline: MKPolyline) -> Double {
        guard polyline.pointCount >= 2 else { return 0 }
        let pts = polyline.points()
        return bearing(from: pts[0].coordinate, to: pts[polyline.pointCount - 1].coordinate)
    }

    private func endCoordinate(of polyline: MKPolyline) -> CLLocationCoordinate2D {
        guard polyline.pointCount > 0 else {
            return CLLocationCoordinate2D(latitude: 0, longitude: 0)
        }
        return polyline.points()[polyline.pointCount - 1].coordinate
    }

    private func bearing(from a: CLLocationCoordinate2D,
                         to b: CLLocationCoordinate2D) -> Double {
        let lat1 = a.latitude * .pi / 180
        let lat2 = b.latitude * .pi / 180
        let dLon = (b.longitude - a.longitude) * .pi / 180
        let y = sin(dLon) * cos(lat2)
        let x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon)
        let rad = atan2(y, x)
        return (rad * 180 / .pi + 360).truncatingRemainder(dividingBy: 360)
    }
}