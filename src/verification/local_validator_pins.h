#pragma once
#include <QStringList>
namespace pgn::verification {
// Qualified private archive-checker builds, NOT a consensus trust list or a
// network proof. Preserve old receipts when a later checker is qualified.
inline QStringList localArchiveValidatorPins(){return {
    "c6174c82cf113d45d9df2d5dde396d7269e2b197c0499e0896e10ecc180bab1f",
    "62485dc63405d9cf191135d383f44dd72bb8363659cfadc56897dd5dd03e1b8b",
    // ROOT477: current build, reviewed bounded MR12/interior implementation,
    // existing adversarial/CLI tests plus sixteen actual source pairs passed.
    // This is local receipt qualification, never consensus/network authority.
    "82b1f67d8ff19f4c508aadbed518f3fa63849564ad203447511fb88eebd4178a"};}
}
