import Foundation
import CoreBluetooth

// Nordic UART Service (NUS) — protokol yang sama dengan firmware MuchRacing.
// Service  : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
// RX (tulis): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E  -> kirim JSON ke sini
// TX (baca) : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E  -> notifikasi dari device
private let kNavServiceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
private let kNavRxUUID      = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
private let kNavTxUUID      = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

// Nama perangkat yang diiklankan firmware.
private let kTargetName = "MuchRacing-Nav"

@MainActor
final class BLEManager: NSObject, ObservableObject {

    enum Status: Equatable {
        case idle
        case scanning
        case connecting(String)
        case connected(String)
        case failed(String)

        var text: String {
            switch self {
            case .idle:          return "Menunggu"
            case .scanning:      return "Memindai MuchRacing-Nav..."
            case .connecting(let n): return "Menghubungkan ke \(n)..."
            case .connected(let n):  return "Terhubung ke \(n) ✓"
            case .failed(let m):     return m
            }
        }

        var isConnected: Bool {
            if case .connected = self { return true }
            return false
        }

        var color: (red: Double, green: Double, blue: Double) {
            switch self {
            case .connected:  return (0.20, 0.78, 0.35) // hijau
            case .failed:     return (0.90, 0.30, 0.26) // merah
            case .scanning,
                 .connecting: return (0.95, 0.65, 0.10) // oranye
            default:          return (0.55, 0.55, 0.58) // abu-abu
            }
        }
    }

    @Published var status: Status = .idle
    @Published var lastSent: String = ""
    @Published var rxLog: String = ""

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var rxCharacteristic: CBCharacteristic?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // Dipanggil dari UI (tombol / onAppear). Jika Bluetooth sudah nyala -> scan.
    func start() {
        if central.state == .poweredOn {
            scan()
        }
    }

    func scan() {
        guard central.state == .poweredOn else { return }
        status = .scanning
        // Filter scan berdasarkan service UUID NUS -> lebih cepat & hemat baterai.
        central.scanForPeripherals(withServices: [kNavServiceUUID], options: nil)
    }

    func disconnect() {
        if let peripheral {
            central.cancelPeripheralConnection(peripheral)
        }
    }

    func clearLog() {
        rxLog = ""
        lastSent = ""
    }

    // Kirim satu baris JSON ke karakteristik RX. Data otomatis dipecah
    // sesuai MTU agar aman untuk baris yang panjang.
    // Baris selalu diakhiri '\n' karena firmware baru memproses baris
    // setelah menerima newline (lihat NavigationManager.cpp).
    func send(_ json: String) {
        guard let peripheral, let rxCharacteristic else {
            status = .failed("Belum terhubung. Scan dulu.")
            return
        }
        var payload = json
        if !payload.hasSuffix("\n") {
            payload += "\n"
        }
        let data = Data(payload.utf8)
        var mtu = peripheral.maximumWriteValueLength(for: .withResponse)
        if mtu <= 0 { mtu = 182 }

        var offset = 0
        while offset < data.count {
            let len = min(mtu, data.count - offset)
            let chunk = data.subdata(in: offset..<(offset + len))
            peripheral.writeValue(chunk, for: rxCharacteristic, type: .withResponse)
            offset += len
        }
        lastSent = json
    }

    // Kirim satu langkah navigasi (icon + jarak + teks).
    func sendManeuver(icon: Int, distanceM: Int, text: String) {
        let escaped = text
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
        send("{\"icon\":\(icon),\"dist\":\(distanceM),\"text\":\"\(escaped)\"}")
    }

    // Akhiri navigasi di device (kembali ke layar idle).
    func sendClear() {
        send("{\"event\":\"clear\"}")
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            // Auto-scan hanya saat app pertama kali dibuka (status masih idle).
            if case .idle = status { scan() }
        case .poweredOff:
            status = .failed("Bluetooth iPhone mati. Aktifkan dulu.")
        case .unauthorized:
            status = .failed("Izin Bluetooth belum diberikan. Buka Settings > Privacy > Bluetooth.")
        case .unsupported:
            status = .failed("iPhone ini tidak mendukung Bluetooth LE.")
        default:
            status = .idle
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let name = peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? ""
        guard name.contains(kTargetName) else { return }

        self.peripheral = peripheral
        central.stopScan()
        status = .connecting(name)
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.delegate = self
        peripheral.discoverServices([kNavServiceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        status = .failed("Gagal terhubung: \(error?.localizedDescription ?? "tidak diketahui")")
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        rxCharacteristic = nil
        status = .failed("Terputus (\(error?.localizedDescription ?? "oleh device")). Ketuk Scan untuk konek lagi.")
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == kNavServiceUUID }) else {
            status = .failed("Service NUS tidak ditemukan di device.")
            return
        }
        peripheral.discoverCharacteristics([kNavRxUUID, kNavTxUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        for ch in service.characteristics ?? [] {
            if ch.uuid == kNavRxUUID {
                rxCharacteristic = ch
            } else if ch.uuid == kNavTxUUID {
                peripheral.setNotifyValue(true, for: ch)
            }
        }
        guard rxCharacteristic != nil else {
            status = .failed("Karakteristik RX tidak ditemukan.")
            return
        }
        status = .connected(peripheral.name ?? kTargetName)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            rxLog += "Gagal subscribe notifikasi: \(error.localizedDescription)\n"
        }
    }

    // Pesan yang dikirim device (TX / notify) — saat ini hanya dicatat di log.
    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard let value = characteristic.value, error == nil else { return }
        let text = String(decoding: value, as: UTF8.self)
        rxLog += text + "\n"
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            rxLog += "Gagal kirim: \(error.localizedDescription)\n"
        }
    }
}