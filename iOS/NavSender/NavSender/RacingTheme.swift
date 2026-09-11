import SwiftUI
import UIKit

// MARK: - Tema visual ala aplikasi balap (neon racing)
//
// Palet gelap pekat + aksen neon oranye/merah, kartu dengan sudut
// miring "speed", angka digital menyala, dan strip bendera kotak-kotak.

enum RacingTheme {
    static let background = Color(red: 0.035, green: 0.04, blue: 0.05)
    static let card = Color(red: 0.085, green: 0.095, blue: 0.11)
    static let cardBorder = Color(red: 0.24, green: 0.25, blue: 0.28)
    static let cardBorderDark = Color(red: 0.16, green: 0.17, blue: 0.19)

    static let neonOrange = Color(red: 1.00, green: 0.44, blue: 0.00)
    static let neonRed = Color(red: 1.00, green: 0.16, blue: 0.12)
    static let neonYellow = Color(red: 1.00, green: 0.82, blue: 0.00)
    static let neonGreen = Color(red: 0.16, green: 0.95, blue: 0.45)
    static let neonCyan = Color(red: 0.00, green: 0.85, blue: 1.00)
    static let neonMagenta = Color(red: 1.00, green: 0.20, blue: 0.75)

    // Kartu "speed": dua sudut terpotong sehingga terlihat miring ke depan.
    static var racingShape: UnevenRoundedRectangle {
        UnevenRoundedRectangle(topLeadingRadius: 4,
                               bottomLeadingRadius: 16,
                               bottomTrailingRadius: 4,
                               topTrailingRadius: 16)
    }

    static var tagShape: UnevenRoundedRectangle {
        UnevenRoundedRectangle(topLeadingRadius: 2,
                               bottomLeadingRadius: 8,
                               bottomTrailingRadius: 2,
                               topTrailingRadius: 8)
    }
}

extension Color {
    static let neonOrange = RacingTheme.neonOrange
    static let neonRed = RacingTheme.neonRed
    static let neonYellow = RacingTheme.neonYellow
    static let neonGreen = RacingTheme.neonGreen
    static let neonCyan = RacingTheme.neonCyan
    static let neonMagenta = RacingTheme.neonMagenta
    static let racingCard = RacingTheme.card
    static let racingBorder = RacingTheme.cardBorder
}

extension Font {
    // Angka digital khas speedometer balap.
    static func racingDigits(_ size: CGFloat, weight: Font.Weight = .heavy) -> Font {
        .system(size: size, weight: weight, design: .rounded).monospacedDigit()
    }
}

extension View {
    // Efek "menyala" neon untuk teks / ikon.
    func neonGlow(_ color: Color, radius: CGFloat = 8) -> some View {
        self.shadow(color: color.opacity(0.75), radius: radius)
    }
}

// Buka Pengaturan iOS untuk halaman aplikasi ini (digunakan saat akses diblokir).
func openSystemSettings() {
    guard let url = URL(string: UIApplication.openSettingsURLString) else { return }
    UIApplication.shared.open(url)
}

// MARK: - Latar belakang layar

struct RacingBackground: View {
    var body: some View {
        ZStack {
            RacingTheme.background
            // Glow oranye samar diagonal khas layar telemetri.
            LinearGradient(colors: [RacingTheme.neonOrange.opacity(0.07), .clear, .clear],
                           startPoint: .topLeading, endPoint: .bottomTrailing)
            LinearGradient(colors: [.clear, .clear, RacingTheme.neonRed.opacity(0.05)],
                           startPoint: .topLeading, endPoint: .bottomTrailing)
        }
        .ignoresSafeArea()
    }
}

// MARK: - Kartu angular dengan aksen neon opsional

struct RacingCard<Content: View>: View {
    var accent: Color? = nil
    var glow = false
    @ViewBuilder var content: Content

    var body: some View {
        content
            .background(
                ZStack {
                    RacingTheme.racingShape.fill(RacingTheme.card)
                    if let accent {
                        Rectangle()
                            .fill(accent)
                            .frame(width: 3.5)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .clipShape(RacingTheme.racingShape)
                    }
                    RacingTheme.racingShape.stroke(accent?.opacity(0.45) ?? RacingTheme.cardBorder, lineWidth: 1)
                }
                .shadow(color: .black.opacity(0.55), radius: 5, y: 2)
            )
            .compositingGroup()
            .shadow(color: glow ? (accent ?? RacingTheme.neonOrange).opacity(0.30) : .clear, radius: 12)
    }
}

// MARK: - Badge kecil bertema racing

struct RacingBadge: View {
    let text: String
    var color: Color = RacingTheme.neonOrange
    var icon: String? = nil

    var body: some View {
        HStack(spacing: 4) {
            if let icon {
                Image(systemName: icon).font(.caption2.weight(.bold))
            }
            Text(text)
                .font(.caption2.weight(.bold))
                .tracking(0.6)
        }
        .foregroundColor(color)
        .padding(.horizontal, 9)
        .padding(.vertical, 4)
        .background(RacingTheme.tagShape.fill(color.opacity(0.14)))
        .overlay(RacingTheme.tagShape.stroke(color.opacity(0.55), lineWidth: 1))
        .neonGlow(color, radius: 4)
    }
}

// MARK: - Strip bendera kotak-kotak (aksen finis)

struct CheckeredStrip: View {
    var height: CGFloat = 6
    var opacity: Double = 0.35

    var body: some View {
        GeometryReader { geo in
            let cols = 18
            HStack(spacing: 0) {
                ForEach(0..<cols, id: \.self) { i in
                    Rectangle()
                        .fill(i % 2 == 0 ? Color.white : Color.black)
                        .frame(width: geo.size.width / CGFloat(cols))
                }
            }
        }
        .frame(height: height)
        .mask(LinearGradient(colors: [.black, .black, .clear], startPoint: .leading, endPoint: .trailing))
        .opacity(opacity)
    }
}

// MARK: - Baris angka besar ala panel balap (label kecil di atas, angka neon)

struct RacingStat: View {
    let label: String
    let value: String
    var color: Color = .white
    var glowColor: Color = RacingTheme.neonOrange
    var valueFont: Font = .racingDigits(20)

    var body: some View {
        VStack(spacing: 3) {
            Text(label)
                .font(.caption2.weight(.bold))
                .tracking(0.8)
                .foregroundColor(.secondary)
            Text(value)
                .font(valueFont)
                .foregroundColor(color)
                .lineLimit(1)
                .minimumScaleFactor(0.6)
                .neonGlow(glowColor, radius: 6)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 10)
        .background(RacingTheme.racingShape.fill(RacingTheme.card.opacity(0.6)))
        .overlay(RacingTheme.racingShape.stroke(RacingTheme.cardBorderDark, lineWidth: 1))
    }
}