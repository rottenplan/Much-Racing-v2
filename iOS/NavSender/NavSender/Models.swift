import Foundation
import CoreLocation

// MARK: - Telemetri live dari device (GET /api/live)

struct LiveTelemetry: Codable, Equatable {
    var speed: Double
    var rpm: Double
    var trip: Double
    var sats: Int
    var lat: Double
    var lng: Double
    var bat_voltage: Double
    var bat_percent: Int
    var is_charging: Bool

    var coordinate: CLLocationCoordinate2D { CLLocationCoordinate2D(latitude: lat, longitude: lng) }
}

// MARK: - Daftar file sesi (GET /api/sessions)

struct SessionFileInfo: Codable, Identifiable, Hashable {
    let name: String
    let size: String
    let path: String
    var id: String { path }
}

// MARK: - Titik telemetri (LogPacket sessions)

struct TelemetryPoint: Identifiable {
    var id: Int { Int(time) }
    let time: Double      // ms sejak sesi
    let lat: Double
    let lng: Double
    let speed: Double     // km/h
    let rpm: Double
    let accX: Double      // G
    let accY: Double
    let accZ: Double
    let sats: Int
    let fix: Int
    let battery: Int
    let tilt: Double      // derajat

    var coordinate: CLLocationCoordinate2D { CLLocationCoordinate2D(latitude: lat, longitude: lng) }
}

struct LapRecord: Identifiable {
    var id: Int { lapNumber }
    let lapNumber: Int
    let timeMs: Int
    let s1Ms: Int?
    let s2Ms: Int?
    var s3Ms: Int? {
        guard let s1 = s1Ms, let s2 = s2Ms else { return nil }
        return max(0, timeMs - s1 - s2)
    }
}

struct DragStats: Equatable {
    var time0to60: Double?
    var time0to100: Double?
    var time100to200: Double?
    var time402m: Double?

    static let empty = DragStats(time0to60: nil, time0to100: nil, time100to200: nil, time402m: nil)
}

struct SessionAnalysis: Identifiable {
    var id: String { name }
    let name: String
    let date: Date
    let points: [TelemetryPoint]
    let laps: [LapRecord]
    let maxSpeed: Double
    let avgSpeed: Double
    let maxRpm: Double
    let totalDistance: Double   // meter
    let totalTime: Double       // detik
    let bestLapMs: Int?
    let sessionType: String
    let drag: DragStats

    // MARK: - Turunan

    func rankOf(lap: LapRecord) -> Int {
        let sorted = laps.sorted { $0.timeMs < $1.timeMs }
        return (sorted.firstIndex { $0.lapNumber == lap.lapNumber } ?? 0) + 1
    }

    func gapToBest(lap: LapRecord) -> Double {
        guard let best = bestLapMs else { return 0 }
        return Double(lap.timeMs - best) / 1000.0
    }
}