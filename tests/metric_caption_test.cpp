#include "ui/render/card_metric_caption.h"
#include "ui/render/metric_caption.h"
#include "ui/render/horizon_device_visuals.h"
#include "ui/render/gauge_layout.h"
#include <array>
#include <iostream>
#include <limits>

int main()
{
    unsigned checks{}, failures{};
    const auto check = [&](bool ok, const char* label) {
        ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << label << '\n'; }
    };
    using pgn::ui::compactMetricCaption;
    using pgn::ui::telemetryAgeCaption;
    const std::optional<qint64> ahead{-247};
    check(telemetryAgeCaption(ahead)==QStringLiteral("—"),"future UTC does not become negative or invented zero age");
    check(ahead&&*ahead==-247,"raw clock difference is preserved");
    check(telemetryAgeCaption(std::nullopt)==QStringLiteral("—"),"unknown age stays unavailable");
    check(telemetryAgeCaption(0)==QStringLiteral("0ms"),"measured zero age stays zero");
    check(telemetryAgeCaption(247,true)==QStringLiteral("≥247ms"),"certified lower-bound caption preserved");
    struct Example { double raw; const char* text; };
    for (const auto& example : std::array{
        Example{0, "0"}, Example{0.001, "<0.01"}, Example{0.125, "≈0.13"},
        Example{1, "1"}, Example{12.5, "12.5"}, Example{999, "999"},
        Example{999.5, "≈1K"}, Example{1e3, "1K"}, Example{1e6, "1M"},
        Example{746e6, "746M"}, Example{224e9, "224G"}, Example{240e9, "240G"},
        Example{999.5e6, "≈1G"}, Example{1.4e12, "1.4T"}, Example{1e15, "1P"},
        Example{1e18, "1E"}, Example{1e21, "1Z"}, Example{1e24, "≈1Y"}, Example{1e27, "≈1e27"}}) {
        const std::optional<double> raw{example.raw};
        check(compactMetricCaption(raw) == QString::fromUtf8(example.text), "common caption vector");
        check(raw && *raw == example.raw, "presentation never changes raw rate");
    }
    for (const auto invalid : {-1.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
        check(compactMetricCaption(invalid) == QStringLiteral("—"), "invalid is unavailable, not positive/zero");
    check(compactMetricCaption(std::nullopt) == QStringLiteral("—"), "missing remains unavailable");
    check(compactMetricCaption(std::numeric_limits<double>::max()).size() <= 8, "extreme caption bounded");

    for(const auto example:{Example{0.,"0.00"},Example{1.1e12,"1.10T"},Example{3.5,"3.50"},Example{999.5,"999.50"},Example{1e24,"1.00Y"}}){
        const auto text=pgn::ui::compactCardMetricCaption(example.raw);
        check(text==QString::fromUtf8(example.text),"card fixed two decimals with no approximation mark");
        check(!text.contains(QChar(0x2248))&&!text.contains('~'),"no approximation mark on cards");
    }
    check(pgn::ui::fixedCardMetricCaption(.004)=="0.00","card yield retains two decimals");
    check(pgn::ui::compactCardMetricCaption(std::nullopt)==QStringLiteral("—"),"missing card sample stays missing");
    check(pgn::ui::cardMetricText(QString::fromUtf8("≈ 1.23 × 2^47"))==QString::fromUtf8("1.23 × 2^47"),"total-card footer has no approximation mark");
    QHash<QString, QColor> colors;
    const QColor total = HorizonDeviceVisuals::totalCardAccent();
    for (int i = 0; i < 1024; ++i) {
        const auto id = QString("hardware-%1").arg(i);
        const auto color = HorizonDeviceVisuals::identityColor(colors, id, "GPU");
        check(color.isValid() && color.rgb() != total.rgb(), "summary accent never assigned to hardware");
        check(HorizonDeviceVisuals::identityColor(colors, id, "GPU") == color, "hardware identity stays stable");
    }
    colors.insert("reserved-collision", total);
    check(HorizonDeviceVisuals::identityColor(colors, "reserved-collision", "GPU").rgb() != total.rgb(),
          "reserved accent cannot survive an existing hardware cache entry");
    for (double width : {120.0, 240.0, 338.0, 640.0}) {
        double previous = 0;
        for (int i = 0; i != 4; ++i) {
            const auto gauge = FrontierGaugeGeometryForCard(7, width, i, 4, 78, 22);
            check(gauge.radius >= 0 && gauge.radius <= 22 && gauge.labelLeft >= 7 &&
                  gauge.labelRight <= 7 + width, "all summary gauges use bounded device geometry");
            if (i) check(std::abs(gauge.centerX - previous - gauge.meterWidth) < 1e-9,
                         "identical meter spacing");
            previous = gauge.centerX;
        }
    }
    std::cout << "Metric presentation: checks=" << checks << " failures=" << failures
              << "; no application/window/platform plugin, raw telemetry untouched\n";
    return failures ? 1 : 0;
}
