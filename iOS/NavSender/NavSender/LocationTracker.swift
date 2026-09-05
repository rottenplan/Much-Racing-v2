import Foundation
import CoreLocation

// Pelacak lokasi untuk navigasi live. Status & posisi terakhir dipublikasikan
// agar view SwiftUI bisa bereaksi (jarak ke belokan, auto-advance langkah).
@MainActor
final class LocationTracker: NSObject, ObservableObject {

    @Published var authorization: CLAuthorizationStatus = .notDetermined
    @Published var lastLocation: CLLocation?

    private let manager = CLLocationManager()

    override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyBest
        manager.activityType = .automotiveNavigation
    }

    func requestPermission() {
        manager.requestWhenInUseAuthorization()
    }

    func start() {
        guard manager.authorizationStatus == .authorizedWhenInUse ||
              manager.authorizationStatus == .authorizedAlways else { return }
        manager.startUpdatingLocation()
    }

    func stop() {
        manager.stopUpdatingLocation()
    }
}

extension LocationTracker: CLLocationManagerDelegate {

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        authorization = manager.authorizationStatus
        if authorization == .authorizedWhenInUse || authorization == .authorizedAlways {
            manager.startUpdatingLocation()
        }
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        lastLocation = locations.last
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        // Error GPS transien (sinyal hilang dll.) — abaikan; update berikutnya datang sendiri.
    }
}