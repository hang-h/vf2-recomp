#include "vf1_models.h"
#include "rom_zip.h"
#include <chrono>
#include <stdexcept>

namespace vf1_models {
namespace {
using point = std::array<float, 3>;
using mesh = model_overrides::mesh;

// MAME vf's ROM_LOAD, ROM_LOAD16_BYTE and ROM_LOAD32_WORD lanes. Only the
// character records/descriptors, colour tables, polygons and select artwork are needed.
enum { RECORDS, DESCRIPTORS, COLOURS, POLYGONS, GRAPHICS, REGION_COUNT };
struct rom { const char *name; uint32_t crc; int region; size_t offset, size, unit, stride; };
constexpr rom ROMS[] = {
    {"epr-16080.4",  0x3662e1a5, RECORDS, 0, 0x20000, 1, 1},
    {"epr-16082.14", 0xb23f22ee, DESCRIPTORS, 0, 0x80000, 1, 2},
    {"epr-16083.15", 0xd12c77f8, DESCRIPTORS, 1, 0x80000, 1, 2},
    {"mpr-16090.12", 0x90c76831, COLOURS, 0, 0x80000, 1, 2},
    {"mpr-16091.13", 0x53115448, COLOURS, 1, 0x80000, 1, 2},
    {"mpr-16084.6",  0x483f453b, GRAPHICS, 0, 0x80000, 1, 2},
    {"mpr-16085.7",  0x5fa01277, GRAPHICS, 1, 0x80000, 1, 2},
    {"mpr-16096.26", 0xa92b0bf3, POLYGONS, 0x000000, 0x200000, 2, 4},
    {"mpr-16097.27", 0x0232955a, POLYGONS, 0x000002, 0x200000, 2, 4},
    {"mpr-16098.28", 0xcf2e1b84, POLYGONS, 0x400000, 0x200000, 2, 4},
    {"mpr-16099.29", 0x20e46854, POLYGONS, 0x400002, 0x200000, 2, 4},
    {"mpr-16100.30", 0xe13e983d, POLYGONS, 0x800000, 0x200000, 2, 4},
    {"mpr-16101.31", 0x0dbed94d, POLYGONS, 0x800002, 0x200000, 2, 4},
    {"mpr-16102.32", 0x4cb41fb6, POLYGONS, 0xc00000, 0x200000, 2, 4},
    {"mpr-16103.33", 0x526d1c76, POLYGONS, 0xc00002, 0x200000, 2, 4},
};

struct decoder {
    std::vector<uint8_t> regions[REGION_COUNT];

    const uint8_t *bytes(int region, size_t at, size_t count) const
    {
        const auto &r = regions[region];
        if (at > r.size() || count > r.size() - at) throw std::runtime_error("model address outside ROM");
        return r.data() + at;
    }
    const uint8_t *main_bytes(uint32_t a, size_t count) const
    {
        if (a >= 0xfc0000 && a < 0xfe0000) return bytes(RECORDS, a - 0xfc0000, count);
        if (a >= 0x200000 && a < 0x300000) return bytes(DESCRIPTORS, a - 0x200000, count);
        if (a >= 0x1000000 && a < 0x1100000) return bytes(GRAPHICS, a - 0x1000000, count);
        if (a >= 0x1300000 && a < 0x1400000) return bytes(COLOURS, a - 0x1300000, count);
        throw std::runtime_error("unsupported character record address");
    }
    uint32_t word(uint32_t a) const { return rom_zip::le32(main_bytes(a, 4)); }
    float scalar(uint32_t a) const
    {
        const uint32_t bits = word(a);
        float v; memcpy(&v, &bits, 4);
        if (!std::isfinite(v) || std::abs(v) > 1000000) throw std::runtime_error("invalid skeleton length");
        return v;
    }
    uint32_t poly_word(uint32_t a) const { return rom_zip::le32(bytes(POLYGONS, size_t(a) * 4, 4)); }
    point vertex(uint32_t a, float scale_x) const
    {
        point p;
        for (int k = 0; k < 3; ++k) {
            const auto bits = poly_word(a + k);
            memcpy(&p[k], &bits, 4);
            if (!std::isfinite(p[k]) || std::abs(p[k]) > 1000000)
                throw std::runtime_error("invalid model vertex");
        }
        p[0] *= scale_x;
        return p;
    }
    uint32_t colour(uint32_t texture) const
    {
        // VF uploads the colour-index stream at maincpu 0x1300000 to TGP
        // address 0x40000, and the BGR555 palette at 0x2d963c to palette bank
        // 0x1000. Reading these ROM tables gives the same unlit dump_part colours.
        if (texture < 0x40000) throw std::runtime_error("invalid colour-stream address");
        const auto index = rom_zip::le16(bytes(COLOURS, size_t(texture - 0x40000) * 2, 2)) & 0x3ff;
        const auto bgr = rom_zip::le16(main_bytes(0x2d963c + 2 * index, 2));
        const uint32_t r = (bgr & 31) * 255 / 31, g = ((bgr >> 5) & 31) * 255 / 31,
                       b = ((bgr >> 10) & 31) * 255 / 31;
        return 0xff000000 | (r << 16) | (g << 8) | b;
    }
    static void triangle(mesh &out, uint32_t colour, const point &a, const point &b, const point &c)
    {
        double u[3], v[3];
        for (int k = 0; k < 3; ++k) { u[k] = double(b[k]) - a[k]; v[k] = double(c[k]) - a[k]; }
        const double x = u[1]*v[2] - u[2]*v[1], y = u[2]*v[0] - u[0]*v[2], z = u[0]*v[1] - u[1]*v[0];
        if (x*x + y*y + z*z > 1e-20) out.push_back({colour, {a, b, c}});
    }
    void part(mesh &out, uint32_t descriptor, float scale_x = 1) const
    {
        if ((descriptor >> 16) != 0x2d) throw std::runtime_error("invalid model descriptor");
        const uint32_t count = word(descriptor);
        uint32_t tex = word(descriptor + 4), a = word(descriptor + 8);
        if (!count || count > 4096 || a & 0x800000) throw std::runtime_error("unsupported model stream");
        point old0 = vertex(a, scale_x), old1 = vertex(a + 3, scale_x);
        a += 6;
        for (uint32_t i = 0; i < count; ++i, a += 10) {
            const uint32_t flags = poly_word(a), type = flags & 3, link = (flags >> 8) & 3;
            if (!type) break;
            if (flags & 0x1000) ++tex;
            const point next0 = vertex(a + 4, scale_x), next1 = type == 2 ? next0 : vertex(a + 7, scale_x);
            if (link) {
                const uint32_t rgb = colour(tex);
                triangle(out, rgb, old1, old0, next0);
                triangle(out, rgb, old1, next0, next1);
            }
            switch (link) {
            case 0: case 2: old0 = next0; old1 = next1; break;
            case 1: old1 = next0; break;
            case 3: old0 = next1; break;
            }
        }
        if (out.empty() || out.size() > 16384) throw std::runtime_error("empty or oversized model");
    }
    mesh head(uint32_t record, unsigned eye, unsigned brow, unsigned mouth) const
    {
        mesh result;
        const uint32_t lists = word(record + 0x72);
        const unsigned frames[] = {0, 0, 0, 0, eye, brow, eye, brow, mouth};
        for (unsigned p = 0; p < std::size(frames); ++p) {
            // Kage's mask has no separate eyebrow parts (5 and 7). VF's
            // face renderer skips null lists rather than drawing a substitute.
            const uint32_t list = word(lists + 4 * p);
            if (list) part(result, word(list + 4 * frames[p]));
        }
        if (result.empty()) throw std::runtime_error("missing fighter head");
        return result;
    }
};
}

bool load(const std::string &path, model_overrides::table &out, std::string &error, portraits *icons)
{
    try {
        rom_zip::zip z;
        if (!z.open(path)) throw std::runtime_error("cannot read ZIP");
        decoder d;
        d.regions[RECORDS].resize(0x20000);
        d.regions[DESCRIPTORS].resize(0x100000);
        d.regions[COLOURS].resize(0x100000);
        d.regions[POLYGONS].resize(0x1000000);
        d.regions[GRAPHICS].resize(0x100000);
        std::vector<uint8_t> raw;
        for (const auto &ro : ROMS) {
            const rom_zip::entry *e = nullptr;
            for (const auto &candidate : z.entries)
                if (candidate.crc == ro.crc && candidate.usize == ro.size) { e = &candidate; break; }
            if (!e || !z.read(*e, raw))
                throw std::runtime_error(std::string("missing, wrong or damaged VF1 model ROM: ") + ro.name);
            auto &dest = d.regions[ro.region];
            for (size_t i = 0; i < raw.size(); i += ro.unit)
                memcpy(dest.data() + ro.offset + (i / ro.unit) * ro.stride, raw.data() + i, ro.unit);
        }
        // VF1 P1 Akira. These are record offsets and VF2 object addresses only;
        // geometry and colours always come from the player's ROM on this launch.
        constexpr uint32_t record = 0xfd4f99;
        struct binding { uint32_t object, offset; float scale_x; };
        constexpr binding body[] = {
            {0x98c21c, 0x12, 1}, {0x84eb76, 0x1a, 25.f/26}, {0x84fe0c, 0x1e, 25.f/21},
            {0x84e8b9, 0x26, 25.f/26}, {0x84fc17, 0x2a, 25.f/21}, {0x98bdf6, 0x32, 1},
            {0x84f00b, 0x36, 1}, {0x84d971, 0x3a, 1}, {0x84d414, 0x3e, 1},
            {0x84ee66, 0x42, 1}, {0x84d6d2, 0x46, 1}, {0x84d16b, 0x4a, 1},
        };
        model_overrides::table prepared;
        for (const auto &b : body) d.part(prepared[b.object], d.word(record + b.offset), b.scale_x);
        // VF1's first four face parts are fixed; the other five are paired
        // eyes/brows and a mouth. VF2 swaps complete heads. Use its actual
        // displayed head to select the closest VF1 expression, with no timer
        // or gameplay state changes. Frame choices are a visual fit, not a
        // shared animation numbering scheme between the two games.
        const auto neutral = d.head(record, 0, 1, 1);
        // The seven additional head aliases retain the neutral fallback;
        // their expression use has not been observed in the game trace.
        for (uint32_t object : {0x84dc10u, 0x86ff68u, 0x870c11u, 0x996845u, 0x9974eeu,
                               0x998197u, 0x998e40u, 0x999ae9u, 0x99a792u, 0x99b43bu})
            prepared[object] = neutral;
        prepared[0x86ff68] = d.head(record, 0, 1, 9);   // eyes open, teeth showing
        prepared[0x870c11] = d.head(record, 16, 16, 16); // eyes shut, clenched teeth

        // Original P1 costumes, from fd2f1b -> fd2e8f. VF2's displayed
        // player/part chooses these synthetic keys, so both costumes and all
        // head aliases use the same fighter without sharing object addresses.
        constexpr uint32_t records[fighter_count] = {
            0xfd4f99, 0xfd3987, 0xfd2f45, 0xfd3525, 0xfd3b05, 0xfd3231, 0xfd3811, 0xfd49b9
        };
        struct body_part { int slot; uint32_t offset; int bone; };
        constexpr body_part parts[] = {
            {1,0x12,-1}, {3,0x1a,3}, {4,0x1e,4}, {6,0x26,6}, {7,0x2a,7}, {9,0x32,-1},
            {10,0x36,10}, {11,0x3a,11}, {12,0x3e,-1}, {13,0x42,13}, {14,0x46,14}, {15,0x4a,-1}
        };
        // VF2's actual joint distances, measured from each fighter's displayed
        // matrices. VF1's lengths are read from its record, not guessed from
        // the mesh bounds (clothes extend beyond the joints).
        constexpr float lengths[fighter_count][4] = {
            {.25f,.25f,.39f,.44f}, {.23f,.23f,.37f,.42f}, {.25f,.25f,.39f,.44f}, {.26f,.27f,.39f,.44f},
            {.25f,.26f,.39f,.44f}, {.25f,.25f,.39f,.44f}, {.23f,.24f,.39f,.44f}, {.25f,.25f,.39f,.44f}
        };
        constexpr float head_x[fighter_count] = {.37f,.31f,.37f,.39f,.40f,.37f,.32f,.37f};
        constexpr float shoulder_x[fighter_count] = {.28f,.25f,.28f,.30f,.30f,.28f,.25f,.28f};
        constexpr float shoulder_z[fighter_count] = {.24f,.17f,.24f,.26f,.24f,.24f,.17f,.24f};
        for (int fighter = 0; fighter < fighter_count; ++fighter) {
            if (!fighter) {
                // Preserve the validated Akira geometry and proportions exactly.
                for (size_t p = 0; p < std::size(parts); ++p)
                    prepared[key(0, parts[p].slot)] = prepared.at(body[p].object);
                prepared[key(0, 2)] = neutral;
                continue;
            }
            const uint32_t rec = records[fighter], skeleton = d.word(rec);
            for (const auto &p : parts) {
                float scale = 1;
                if (p.bone >= 0) {
                    const int kind = p.slot == 3 || p.slot == 6 ? 0 : p.slot == 4 || p.slot == 7 ? 1 :
                                     p.slot == 10 || p.slot == 13 ? 2 : 3;
                    const float from = d.scalar(skeleton + 12 * p.bone);
                    if (!(from > .1f && from < 1.f)) throw std::runtime_error("unsupported fighter skeleton");
                    scale = lengths[fighter][kind] / from;
                }
                auto &model = prepared[key(fighter, p.slot)];
                d.part(model, d.word(rec + p.offset), scale);
                if (p.slot == 1) {
                    // Fit the torso at both shoulder and neck anchors. A single
                    // length scale would move the shoulders away from the arms.
                    const float neck = d.scalar(skeleton + 12), shoulder = d.scalar(skeleton + 24);
                    const float width = std::abs(d.scalar(skeleton + 32));
                    if (!(neck > shoulder && shoulder > .1f && width > .1f))
                        throw std::runtime_error("unsupported torso skeleton");
                    for (auto &t : model) for (auto &v : t.points) {
                        v[0] = v[0] <= shoulder ? v[0] * shoulder_x[fighter] / shoulder :
                            shoulder_x[fighter] + (v[0] - shoulder) * (head_x[fighter] - shoulder_x[fighter]) / (neck - shoulder);
                        v[2] *= shoulder_z[fighter] / width;
                    }
                }
            }
            // Kage's mouth list starts with his blue face covering. Entries
            // 1 onward are bare-mouth animation meshes and must not be used
            // with the masked VF1 costume, including during a blink.
            const unsigned neutral_mouth = fighter == 5 ? 0 : 1;
            prepared[key(fighter, 2)] = d.head(rec, 0, 1, neutral_mouth);
            // VF2's common head table closes the eyelids in two steps. Keep
            // brows and mouth still while blinking; Jacky's alternate heads
            // also open his mouth. These are visual matches between games,
            // not an assumption that their expression frame numbers agree.
            const unsigned mouth = fighter == 7 ? 9 : neutral_mouth;
            prepared[key(fighter, 16)] = d.head(rec, 8, 1, mouth);
            prepared[key(fighter, 17)] = d.head(rec, 16, 1, mouth);
        }

        portraits pictures;
        if (icons) for (int fighter = 0; fighter < fighter_count; ++fighter) {
            // Akira's updated icon is byte-indexed in the descriptor ROM. The
            // other seven use word-indexed maps in bank 3, with their tiles and
            // palettes in bank 0. All are original 7x8, 4bpp select artwork.
            struct icon_source { uint32_t map, graphics, palette; unsigned first, count; int crop_x; };
            constexpr icon_source sources[fighter_count] = {
                {0x2ff5ec, 0x2fef8c, 0x2ff5cc, 0,      50, 9},
                {0x13d1997,0x107f704,0x1082b44,0x2480,49,17},
                {0x13d191b,0x107f0c0,0x1082b24,0x2400,50,13},
                {0x13d1b87,0x1081174,0x1082bc4,0x2680,49,13},
                {0x13d1b0b,0x1080a90,0x1082ba4,0x2600,55,17},
                {0x13d1c03,0x1081798,0x1082be4,0x2700,56,17},
                {0x13d1a13,0x107fd28,0x1082b64,0x2500,53, 9},
                {0x13d189f,0x107d910,0x1082aa4,0x2200,49, 9}
            };
            const auto &source = sources[fighter];
            auto &picture = pictures[fighter];
            picture.crop_x = source.crop_x;
            if (d.word(source.map) != (fighter ? 0x18000u : 0xb080u) ||
                d.word(source.map + 4) != 8 || d.word(source.map + 8) != 7)
                throw std::runtime_error("unsupported fighter portrait map");
            for (int y = 0; y < portrait::height; ++y) for (int x = 0; x < portrait::width; ++x) {
                const unsigned cell = (y / 8) * 7 + x / 8;
                const unsigned tile = (fighter ? rom_zip::le16(d.main_bytes(source.map + 12 + 2 * cell, 2)) :
                    *d.main_bytes(source.map + 12 + cell, 1)) - source.first;
                if (tile >= source.count) throw std::runtime_error("invalid portrait tile");
                const auto *row = d.main_bytes(source.graphics + tile * 32 + (y % 8) * 4, 4);
                const uint32_t packed = (uint32_t(rom_zip::le16(row)) << 16) | rom_zip::le16(row + 2);
                const unsigned pen = (packed >> (28 - 4 * (x % 8))) & 15;
                const uint16_t bgr = rom_zip::le16(d.main_bytes(source.palette + pen * 2, 2));
                uint32_t rgb = 0xff000000;
                for (int c = 0; c < 3; ++c) {
                    const unsigned v = (bgr >> (5 * c)) & 31;
                    unsigned channel = (v << 3) | (v >> 2); // Model 1's pal5bit
                    if (!(bgr & 0x8000)) channel >>= 1;
                    rgb |= channel << (16 - 8 * c);
                }
                picture.pixels[y * portrait::width + x] = rgb;
            }
        }
        out.swap(prepared);
        if (icons) *icons = pictures;
        error.clear();
        return true;
    } catch (const std::runtime_error &e) {
        error = "VF1 models (" + path + "): " + e.what();
        return false;
    }
}

bool prepare(const std::string &vf2_zip, model_overrides::table &out, std::string &error, portraits *icons)
{
    namespace fs = std::filesystem;
    const auto folder = fs::path(vf2_zip).parent_path();
    const fs::path candidates[] = {folder / "vf.zip", folder.filename() == "roms"
        ? folder.parent_path() / "vf.zip" : folder / "roms" / "vf.zip"};
    for (const auto &path : candidates) {
        std::error_code ec;
        const bool exists = fs::exists(path, ec);
        if (ec) { error = "Cannot check VF1 companion: " + path.string() + ": " + ec.message(); return false; }
        if (!exists) continue;
        const auto start = std::chrono::steady_clock::now();
        if (!load(path.string(), out, error, icons)) return false;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        fprintf(stderr, "VF1 models: prepared %zu VF2 object bindings from %s in %lld ms\n",
                out.size(), path.string().c_str(), static_cast<long long>(ms));
        return true;
    }
    out.clear();
    if (icons) *icons = {};
    error.clear();
    return true;
}
}
