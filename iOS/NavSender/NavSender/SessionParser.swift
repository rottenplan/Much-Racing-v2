import Foundation
import CoreLocation

// MARK: - Parser sesi device (SD card)
//
// File di SD berisi biner LogPacket 32 byte (header 0xAA55 LE) yang diselingi
// baris ASCII: "LAP,<no>,<ms>", "SECTOR,<no>,<1|2>,<ms>", dan "DRAG,...".
//
// LogPacket (32 byte, little-endian):
//   0  u16  header      = 0xAA55
//   2  u32  timestamp   ms
//   6  i32  lat         1e7
//   10 i32  lon         1e7
//   14 u16  speed       km/h * 10
//   16 u16  rpm
//   18 i16  accX        G * 100
//   20 i16  accY        G * 100
//   22 i16  accZ        G * 100
//   24 u8   sats
//   25 u8   fix
//   26 u8   battery     %
//   27 i16  tilt        derajat * 10
//   29 u8   checksum
//   30 u8   padding

enum LogParser {
    struct RawPacket {
        let time: Double
        let lat: Double
        let lng: Double
        let speed: Double
        let rpm: Double
        let accX: Double
        let accY: Double
        let accZ: Double
        let sats: Int
        let fix: Int
        let battery: Int
        let tilt: Double
    }

    static func parse(_ data: Data, fileName: String) -> SessionAnalysis {
        var points: [TelemetryPoint] = []
        var laps: [LapRecord] = []
        var sectorByLap: [Int: (Int?, Int?)] = [:]
        let bytes = [UInt8](data)
        var i = 0

        var bit = ""
        while i < bytes.count {
            if i + 32 <= bytes.count, bytes[i] == 0x55, bytes[i + 1] == 0xAA {
                let p = decodePacket(bytes, at: i)
                points.append(TelemetryPoint(time: p.time,
                                             lat: p.lat, lng: p.lng,
                                             speed: p.speed, rpm: p.rpm,
                                             accX: p.accX, accY: p.accY, accZ: p.accZ,
                                             sats: p.sats, fix: p.fix,
                                             battery: p.battery, tilt: p.tilt))
                i += 32
                continue
            }

            let c = bytes[i]
            if c == 0x0A {
                parseLine(bit, laps: &laps, sectors: &sectorByLap)
                bit = ""
            } else if !(c == 0x0D || c < 0x20) {
                bit.append(Character(UnicodeScalar(c)))
            }
            i += 1
        }
        if !bit.isEmpty { parseLine(bit, laps: &laps, sectors: &sectorByLap) }

        laps = laps.enumerated().compactMap { idx, lap in
            let secs = sectorByLap[lap.lapNumber] ?? (nil, nil)
            return LapRecord(lapNumber: idx + 1, timeMs: lap.timeMs, s1Ms: secs.0, s2Ms: secs.1)
        }

        let maxSpeed = points.map(\.speed).max() ?? 0
        let maxRpm = points.map(\.rpm).max() ?? 0
        let avgSpeed = points.isEmpty ? 0 : points.map(\.speed).reduce(0, +) / Double(points.count)
        let totalDistance = trackDistance(points)
        let totalTime = (points.last?.time ?? 0) - (points.first?.time ?? 0)
        let bestLapMs = laps.map(\.timeMs).min()
        let drag = dragStats(from: points)

        // Anggap session disimpan ~waktu file dibuat (untuk display saja)
        var date = Date()
        if let fstat = try? FileManager.default.attributesOfItem(atPath: fileName) {
            date = (fstat[.creationDate] as? Date) ?? date
        }

        return SessionAnalysis(name: fileName,
                               date: date,
                               points: points,
                               laps: laps,
                               maxSpeed: maxSpeed,
                               avgSpeed: avgSpeed,
                               maxRpm: maxRpm,
                               totalDistance: totalDistance,
                               totalTime: totalTime / 1000.0,
                               bestLapMs: bestLapMs,
                               sessionType: fileName.uppercased().contains("DRAG") ? "DRAG" : "TRACK",
                               drag: drag)
    }

    // MARK: - Packet decode

    private static func decodePacket(_ b: [UInt8], at i: Int) -> RawPacket {
        let u16 = { (o: Int) -> UInt16 in UInt16(b[i + o]) | (UInt16(b[i + o + 1]) << 8) }
        let u32 = { (o: Int) -> UInt32 in
            UInt32(b[i + o]) | (UInt32(b[i + o + 1]) << 8) | (UInt32(b[i + o + 2]) << 16) | (UInt32(b[i + o + 3]) << 24)
        }
        let i32 = { (o: Int) -> Int32 in Int32(bitPattern: u32(o)) }
        let i16 = { (o: Int) -> Int16 in Int16(bitPattern: u16(o)) }

        return RawPacket(time: Double(u32(2)),
                         lat: Double(i32(6)) / 1e7,
                         lng: Double(i32(10)) / 1e7,
                         speed: Double(u16(14)) / 10.0,
                         rpm: Double(u16(16)),
                         accX: Double(i16(18)) / 100.0,
                         accY: Double(i16(20)) / 100.0,
                         accZ: Double(i16(22)) / 100.0,
                         sats: Int(b[i + 24]),
                         fix: Int(b[i + 25]),
                         battery: Int(b[i + 26]),
                         tilt: Double(i16(27)) / 10.0)
    }

    // MARK: - Baris metadata

    private static func parseLine(_ line: String, laps: inout [LapRecord], sectors: inout [Int: (Int?, Int?)]) {
        let parts = line.split(separator: ",").map { String($0) }
        guard parts.count >= 2 else { return }

        switch parts[0] {
        case "LAP":
            if parts.count >= 3, let no = Int(parts[1]), let ms = Int(parts[2]) {
                laps.append(LapRecord(lapNumber: no, timeMs: ms, s1Ms: nil, s2Ms: nil))
            }
        case "SECTOR":
            if parts.count >= 4, let no = Int(parts[1]), let which = Int(parts[2]), let ms = Int(parts[3]) {
                var cur = sectors[no] ?? (nil, nil)
                if which == 1 { cur.0 = ms } else if which == 2 { cur.1 = ms }
                sectors[no] = cur
            }
        default:
            break
        }
    }

    // MARK: - Statistik

    private static func haversine(_ a: CLLocationCoordinate2D, _ b: CLLocationCoordinate2D) -> Double {
        let r = 6371000.0
        let dLat = (b.latitude - a.latitude) * .pi / 180
        let dLon = (b.longitude - a.longitude) * .pi / 180
        let la1 = a.latitude * .pi / 180
        let la2 = b.latitude * .pi / 180
        let h = sin(dLat / 2) * sin(dLat / 2) + cos(la1) * cos(la2) * sin(dLon / 2) * sin(dLon / 2)
        return 2 * r * asin(min(1, sqrt(h)))
    }

    private static func trackDistance(_ pts: [TelemetryPoint]) -> Double {
        var total = 0.0
        guard pts.count > 1 else { return 0 }
        for k in 1..<pts.count {
            let d = haversine(pts[k - 1].coordinate, pts[k].coordinate)
            if d < 1000 { total += d } // filter loncatan GPS
        }
        return total
    }

    // Drag: hitung dari titik telemetri. Start = speed >= 5 km/h pertama.
    static func dragStats(from pts: [TelemetryPoint]) -> DragStats {
        guard let startIdx = pts.firstIndex(where: { $0.speed >= 5 }) else { return .empty }
        var t0to60: Double?
        var t0to100: Double?
        var t100to200: Double?
        var t402m: Double?

        var distance: Double = 0
        var last = pts[startIdx].time

        var i = startIdx + 1
        while i < pts.count {
            let dt = (pts[i].time - last) / 1000.0
            distance += ((pts[i].speed + pts[i - 1].speed) / 2) * dt / 3.6
            last = pts[i].time

            let t = (pts[i].time - pts[startIdx].time) / 1000.0
            if t0to60 == nil, pts[i].speed >= 60 { t0to60 = t }
            if t0to100 == nil, pts[i].speed >= 100 { t0to100 = t }
            if t100to200 == nil, pts[i].speed >= 200 { t100to200 = t }
            if t402m == nil, distance >= 402.0 { t402m = t }

            i += 1
        }

        return DragStats(time0to60: t0to60, time0to100: t0to100, time100to200: t100to200, time402m: t402m)
    }
}