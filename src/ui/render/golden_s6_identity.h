#pragma once
#include <QString>

// Selected RC7 payloads. Integrity admission does not authorize an old canary.
namespace GoldenS6Identity {
inline constexpr auto Engine="efc9fe41807af1090e394766aaa8e5c619523cfdd0e20a17733878f83d6bc496";
inline constexpr auto Cuda="8d8230ea8ff5ee26e574f76282525c7d06892d8cb6b88d09a15d5e6f6f55b000";
inline constexpr auto Crt="edb8beb6cc6b347b6a62820858482915623a5cce6d55a2bb7024c38aeba0574e";
inline constexpr auto CrosscoinX008Engine=Engine; // Cooperative-stop protocol name.
inline bool engine(const QString& sha){return sha.compare(QLatin1String(Engine),Qt::CaseInsensitive)==0;}
inline constexpr auto CrosscoinX010Cuda=Cuda;
inline bool cuda(const QString& sha){return sha.compare(QLatin1String(Cuda),Qt::CaseInsensitive)==0;}
inline bool crt(const QString& sha){return sha.compare(QLatin1String(Crt),Qt::CaseInsensitive)==0;}
}
