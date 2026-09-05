import Foundation

// MARK: - Klien HTTP ke AP perangkat (MuchRacing-GPS, 192.168.4.1)
//
// Endpoint device (Firmware WiFiManager.cpp):
//   GET /api/live      -> JSON {speed, rpm, trip, sats, lat, lng, bat_voltage, bat_percent, is_charging}
//   GET /api/sessions  -> JSON array [{name, size, path}]
//   GET /download?file=/sessions/run_N.csv -> file mentah (biner LogPacket + LAP/SECTOR/DRAG)

struct APClient {
    private static let ipStorageKey = "deviceAPURL"

    static func savedBaseURL() -> String {
        UserDefaults.standard.string(forKey: ipStorageKey) ?? "http://192.168.4.1"
    }

    static func saveBaseURL(_ url: String) {
        UserDefaults.standard.set(url, forKey: ipStorageKey)
    }

    var baseURL: String

    init(baseURL: String = APClient.savedBaseURL()) {
        self.baseURL = baseURL
    }

    private func makeRequest(path: String) -> URLRequest {
        let url = URL(string: baseURL + path)!
        var request = URLRequest(url: url)
        request.timeoutInterval = 3
        request.cachePolicy = .reloadIgnoringLocalAndRemoteCacheData
        return request
    }

    func fetchLive() async throws -> LiveTelemetry {
        let (data, response) = try await URLSession.shared.data(for: makeRequest(path: "/api/live"))
        guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
            throw APIError.badStatus
        }
        return try JSONDecoder().decode(LiveTelemetry.self, from: data)
    }

    func fetchSessions() async throws -> [SessionFileInfo] {
        let (data, response) = try await URLSession.shared.data(for: makeRequest(path: "/api/sessions"))
        guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
            throw APIError.badStatus
        }
        return try JSONDecoder().decode([SessionFileInfo].self, from: data)
    }

    func download(path: String) async throws -> Data {
        let encoded = path.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? path
        let (data, response) = try await URLSession.shared.data(for: makeRequest(path: "/download?file=\(encoded)"))
        guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
            throw APIError.badStatus
        }
        return data
    }

    enum APIError: LocalizedError {
        case badStatus
        var errorDescription: String? { "Device tidak merespons dengan benar." }
    }
}