import SwiftUI

// MARK: - Pilihan Rute ala Google Maps: hindari tol / jalan raya.
//
// Dipakai sebelum menghitung rute (tab Navigasi, panel Live, dan Setelan).
// Preferensi tersimpan otomatis lewat UserDefaults di RouteManager sehingga
// konsisten di seluruh tempat hitung rute.

struct RouteOptionsView: View {
    @EnvironmentObject var route: RouteManager

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label("Pilihan Rute", systemImage: "signpost.right.and.left")
                .font(.caption.weight(.bold))
                .tracking(1.1)
                .foregroundColor(.neonYellow)
                .neonGlow(.neonYellow, radius: 4)

            optionRow(title: "Hindari jalan tol", icon: "creditcard.fill", isOn: $route.avoidTolls)
            optionRow(title: "Hindari jalan raya", icon: "road.lanes", isOn: $route.avoidHighways)
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(RacingTheme.card.opacity(0.7))
                .overlay(RoundedRectangle(cornerRadius: 10).stroke(RacingTheme.cardBorder, lineWidth: 1))
        )
    }

    private func optionRow(title: String, icon: String, isOn: Binding<Bool>) -> some View {
        Toggle(isOn: isOn) {
            Label(title, systemImage: icon)
                .font(.subheadline.weight(.medium))
                .foregroundColor(.white)
        }
        .tint(.neonOrange)
    }
}

// MARK: - Pemilih rute alternatif (chips) ala Google Maps
//
// Ditampilkan saat perhitungan menghasilkan lebih dari satu rute. Mengetuk
// chip memilih rute tersebut; callback onSelect dipakai untuk me-restart
// sesi navigasi bila sedang aktif.

struct RouteAlternatives: View {
    @EnvironmentObject var route: RouteManager
    var onSelect: (() -> Void)? = nil

    var body: some View {
        if route.routes.count > 1 {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(Array(route.routes.enumerated()), id: \.offset) { index, r in
                        Button {
                            route.selectRoute(at: index)
                            onSelect?()
                        } label: {
                            VStack(alignment: .leading, spacing: 3) {
                                Text("Rute \(index + 1)")
                                    .font(.caption.weight(.bold))
                                    .foregroundColor(index == route.selectedRouteIndex ? .neonGreen : .white)
                                Text(String(format: "%.1f km • %d mnt",
                                            r.distance / 1000.0,
                                            Int(r.expectedTravelTime / 60)))
                                    .font(.caption2)
                                    .foregroundColor(.secondary)
                                    .monospacedDigit()
                            }
                            .padding(.horizontal, 12)
                            .padding(.vertical, 8)
                            .background(
                                RoundedRectangle(cornerRadius: 8)
                                    .fill(RacingTheme.card.opacity(0.9))
                                    .overlay(
                                        RoundedRectangle(cornerRadius: 8)
                                            .stroke(index == route.selectedRouteIndex
                                                    ? Color.neonGreen.opacity(0.85)
                                                    : RacingTheme.cardBorder,
                                                    lineWidth: 1.5)
                                    )
                            )
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.vertical, 2)
            }
        }
    }
}