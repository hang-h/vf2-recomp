#pragma once
#include "../machine/model_override.h"

namespace vf1_models {
// Select-screen order, shared by both games (Shun and Lion have no VF1 model).
inline constexpr int fighter_count = 8;
// Slots 1..15 follow VF2's skeleton; 16/17 are the half-closed/closed-eye heads.
constexpr uint32_t key(int fighter, int slot) { return 0xf1000000u + uint32_t(fighter) * 32 + slot; }
// Original select icon, decoded directly from the user's VF1 ZIP.
struct portrait {
    static constexpr int width = 56, height = 64;
    std::array<uint32_t, width * height> pixels{};
    int crop_x = 9; // 30x56 face crop, fitted to VF2's 46x86 interior without stretching
    bool available() const { return pixels[0] != 0; }
};
using portraits = std::array<portrait, fighter_count>;
// Decode models and optional portraits from the user's MAME vf ZIP. No emulation, captures,
// extracted files or disk cache. Failure leaves the destination unchanged.
bool load(const std::string &zip, model_overrides::table &out, std::string &error, portraits *icons = nullptr);

// Missing companion is normal VF2. A present but invalid companion reports an error.
// Also supports Documents/vf2.zip with Documents/roms/vf.zip (and vice versa).
bool prepare(const std::string &vf2_zip, model_overrides::table &out, std::string &error, portraits *icons = nullptr);
}
