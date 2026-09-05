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
}

@MainActor
final class RouteManager: ObservableObject {

    @Published var isComputing = false
    @Published var errorMessage: String?
    @Published var route: MKRoute?
    @Published var routeSummary = ""
    @Published var steps: [NavStep] = []

    func reset() {
        route = nil
        routeSummary = ""
        steps = []
        errorMessage = nil
        isComputing = false
    }

    // Cari tempat tujuan lewat pencarian lokal MapKit (tanpa API key).
    func searchDestination(_ query: String,
                           near center: CLLocationCoordinate2D) async throws -> MKMapItem? {
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
                    continuation.resume(returning: response?.mapItems.first)
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
        request.requestsAlternateRoutes = false

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

        guard let firstRoute = response.routes.first else { return false }

        route = firstRoute
        steps = buildSteps(from: firstRoute)
        routeSummary = String(format: "%.1f km • %d menit",
                              firstRoute.distance / 1000.0,
                              Int(firstRoute.expectedTravelTime / 60))
        return true
    }

    // MARK: - Konversi langkah MapKit -> NavStep dengan icon manuver

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