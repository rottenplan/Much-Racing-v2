import SwiftUI

// Daftar manuver dengan kode icon yang dipahami firmware:
// 0=tiba, 1=lurus, 2=kiri sedikit, 3=kiri, 4=kiri tajam,
// 5=kanan sedikit, 6=kanan, 7=kanan tajam, 8=putar balik, 9=bundaran
struct Maneuver: Identifiable {
    let id: Int
    let icon: Int
    let label: String
    let symbol: String
}

let maneuverList: [Maneuver] = [
    Maneuver(id: 1,  icon: 1, label: "Lurus",         symbol: "arrow.up"),
    Maneuver(id: 2,  icon: 2, label: "Kiri Sedikit",  symbol: "arrow.up.left"),
    Maneuver(id: 3,  icon: 3, label: "Belok Kiri",    symbol: "arrow.left"),
    Maneuver(id: 4,  icon: 4, label: "Kiri Tajam",    symbol: "arrow.up.left"),
    Maneuver(id: 5,  icon: 5, label: "Kanan Sedikit", symbol: "arrow.up.right"),
    Maneuver(id: 6,  icon: 6, label: "Belok Kanan",   symbol: "arrow.right"),
    Maneuver(id: 7,  icon: 7, label: "Kanan Tajam",   symbol: "arrow.up.right"),
    Maneuver(id: 8,  icon: 8, label: "Putar Balik",   symbol: "arrow.uturn.down"),
    Maneuver(id: 9,  icon: 9, label: "Bundaran",      symbol: "arrow.triangle.2.circlepath"),
    Maneuver(id: 10, icon: 0, label: "Tiba",          symbol: "flag.checkered"),
]

func defaultText(forIcon icon: Int) -> String {
    switch icon {
    case 0: return "You have arrived"
    case 1: return "Go straight"
    case 2: return "Slight left"
    case 3: return "Turn left"
    case 4: return "Sharp left"
    case 5: return "Slight right"
    case 6: return "Turn right"
    case 7: return "Sharp right"
    case 8: return "Make a U-turn"
    case 9: return "Roundabout"
    default: return ""
    }
}

// Bagian uji manual: kirim JSON satu per satu tanpa rute — berguna untuk
// mengecek device tanpa perlu GPS.
struct ManualTestSection: View {
    @EnvironmentObject var ble: BLEManager

    @State private var distance = 300
    @State private var instruction = ""
    @State private var customJSON = "{\"icon\":6,\"dist\":300,\"text\":\"Turn right\"}"

    private let columns = [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Uji Manual (tanpa rute)")
                .font(.headline)

            maneuverGrid

            HStack {
                Text("Jarak belokan:")
                Spacer()
                Stepper("\(distance) m", value: $distance, in: 0...5000, step: 10)
            }

            TextField("Teks instruksi (opsional, kosong = default)", text: $instruction)
                .textFieldStyle(.roundedBorder)
                .submitLabel(.done)

            VStack(spacing: 8) {
                TextField("JSON custom", text: $customJSON)
                    .textFieldStyle(.roundedBorder)
                    .font(.system(.caption, design: .monospaced))
                    .textInputAutocapitalization(.never)
                    .disableAutocorrection(true)
                    .submitLabel(.send)
                    .onSubmit { ble.send(customJSON) }

                HStack(spacing: 12) {
                    Button {
                        ble.sendClear()
                    } label: {
                        Label("Akhiri (clear)", systemImage: "stop.circle")
                    }
                    .buttonStyle(.bordered)
                    .tint(.red)

                    Button {
                        ble.send(customJSON)
                    } label: {
                        Label("Kirim JSON", systemImage: "paperplane.fill")
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(!ble.status.isConnected)
                }
            }
        }
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color(.secondarySystemBackground)))
    }

    private var maneuverGrid: some View {
        LazyVGrid(columns: columns, spacing: 10) {
            ForEach(maneuverList) { m in
                Button {
                    var text = instruction.trimmingCharacters(in: .whitespacesAndNewlines)
                    if text.isEmpty { text = defaultText(forIcon: m.icon) }
                    ble.sendManeuver(icon: m.icon, distanceM: distance, text: text)
                } label: {
                    VStack(spacing: 6) {
                        Image(systemName: m.symbol)
                            .font(.title2)
                        Text(m.label)
                            .font(.caption)
                            .lineLimit(1)
                            .minimumScaleFactor(0.8)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 12)
                    .background(RoundedRectangle(cornerRadius: 10)
                        .fill(ble.status.isConnected ? Color.accentColor.opacity(0.15) : Color(.tertiarySystemFill)))
                    .overlay(RoundedRectangle(cornerRadius: 10)
                        .stroke(Color.accentColor.opacity(0.4), lineWidth: 1))
                }
                .buttonStyle(.plain)
                .disabled(!ble.status.isConnected)
            }
        }
    }
}