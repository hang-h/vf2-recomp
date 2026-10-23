// Local-ROM regression: startup/import must work using the two ZIPs alone.
#include "../../src/host/romset.h"
#include "../../src/host/vf1_models.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
static void require(bool ok, const std::string &message)
{
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
    if (argc < 3) { std::cerr << "usage: vf1check vf.zip vf2.zip [reference.mesh]\n"; return 2; }
    try {
        model_overrides::table models;
        vf1_models::portraits icons;
        std::string error;
        require(vf1_models::load(argv[1], models, error, &icons), "VF1 decode: " + error);
        const auto reference_icons = icons;
        constexpr size_t expected = 22 + 13 * vf1_models::fighter_count + 2 * (vf1_models::fighter_count-1);
        require(models.size() == expected, "legacy Akira bindings, eight skeletons and new expression heads");
        auto same_icons = [](const auto &a, const auto &b) {
            for (size_t fighter = 0; fighter < a.size(); ++fighter)
                if (a[fighter].pixels != b[fighter].pixels || a[fighter].crop_x != b[fighter].crop_x) return false;
            return true;
        };
        for (int fighter = 0; fighter < vf1_models::fighter_count; ++fighter) {
            require(icons[fighter].available(), "original select icon missing: " + std::to_string(fighter));
            for (int previous = 0; previous < fighter; ++previous)
                require(icons[fighter].pixels != icons[previous].pixels, "duplicate character icon");
            for (int slot : {1,2,3,4,6,7,9,10,11,12,13,14,15}) {
                const auto part = models.find(vf1_models::key(fighter, slot));
                require(part != models.end() && !part->second.empty(), "missing body part for fighter " + std::to_string(fighter));
                for (const auto &triangle : part->second) for (const auto &vertex : triangle.points)
                    for (float coordinate : vertex) require(std::isfinite(coordinate), "non-finite mesh coordinate");
            }
        }
        size_t triangles = 0;
        for (const auto &[object, mesh] : models) triangles += mesh.size();
        std::cout << models.size() << " bindings, " << triangles << " triangles\n";
        auto equal = [](const auto &a, const auto &b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].colour != b[i].colour || a[i].points != b[i].points) return false;
            return true;
        };
        for (int fighter = 1; fighter < vf1_models::fighter_count; ++fighter) {
            const auto &neutral = models.at(vf1_models::key(fighter, 2));
            const auto &tense = models.at(vf1_models::key(fighter, 16));
            const auto &shut = models.at(vf1_models::key(fighter, 17));
            require(!neutral.empty() && !tense.empty() && !shut.empty() &&
                    !equal(neutral, tense) && !equal(neutral, shut) && !equal(tense, shut),
                    "distinct expression heads for fighter " + std::to_string(fighter));
        }
        const auto &neutral = models.at(0x84dc10), &tense = models.at(0x86ff68), &shut = models.at(0x870c11);
        require(!equal(neutral, tense) && !equal(tense, shut) && !equal(neutral, shut), "three distinct expressions");
        for (const auto *head : {&tense, &shut}) {
            // The captured mesh starts with the hair. Check a fixed prefix
            // so changing eyes/mouth cannot replace the entire head.
            require(head->size() == neutral.size(), "expression changed face topology");
            for (size_t i = 0; i < 100; ++i)
                require((*head)[i].colour == neutral[i].colour && (*head)[i].points == neutral[i].points,
                        "expression changed the fixed head geometry");
        }
        if (argc > 3) {
            model_overrides reference;
            require(reference.load(argv[3], error), "reference: " + error);
            require(reference.size() == 22, "legacy Akira reference binding count");
            double worst = 0;
            for (const auto &[object, mesh] : models) {
                if (object >= vf1_models::key(0, 0)) continue; // new per-fighter skeleton bindings
                if (object == 0x86ff68 || object == 0x870c11) continue; // old capture has a fixed expression
                const auto *expected = reference.find(object);
                require(expected && expected->size() == mesh.size(), "reference triangle count at " + std::to_string(object));
                for (size_t i = 0; i < mesh.size(); ++i) {
                    require(mesh[i].colour == (*expected)[i].colour, "reference colour");
                    for (int p = 0; p < 3; ++p) for (int k = 0; k < 3; ++k)
                        worst = std::max(worst, std::abs(double(mesh[i].points[p][k]) - (*expected)[i].points[p][k]));
                }
            }
            // Capture JSON rounded vertices to six significant digits.
            require(worst < 0.000002, "reference vertices differ: " + std::to_string(worst));
            std::cout << "Captured reference: body and neutral faces match; max vertex delta " << worst << '\n';
        }
        const auto root = fs::temp_directory_path() / ("vf1-startup-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(root), "test directory");
        struct cleanup { fs::path root; ~cleanup() { std::error_code ec; fs::remove_all(root, ec); } } clean{root};
        const auto documents = root / "Documents", roms = documents / "roms", source = root / "source";
        fs::create_directories(roms);
        fs::create_directories(source);
        fs::create_symlink(fs::absolute(argv[2]), source / "vf2.zip");
        {
            auto r = romset::load((source / "vf2.zip").string());
            require(r.ok && r.vf1_models.empty() &&
                    std::none_of(r.vf1_portraits.begin(), r.vf1_portraits.end(), [](const auto &icon) { return icon.available(); }),
                    "VF2 without a companion: " + r.error);
        }
        fs::create_symlink(fs::absolute(argv[1]), roms / "vf.zip");
        {
            auto r = romset::import_file((source / "vf2.zip").string(), (roms / "vf2.zip").string());
            require(r.ok && r.vf1_models.size() == expected && same_icons(r.vf1_portraits, reference_icons),
                    "import prepares destination's companion and icon: " + r.error);
        }
        {
            auto r = romset::load((roms / "vf2.zip").string());
            require(r.ok && r.vf1_models.size() == expected && same_icons(r.vf1_portraits, reference_icons),
                    "startup from both ZIPs: " + r.error);
        }
        // Legacy Documents/vf2.zip still finds the companion in Documents/roms.
        fs::rename(roms / "vf2.zip", documents / "vf2.zip");
        require(vf1_models::prepare((documents / "vf2.zip").string(), models, error) && models.size() == expected,
                "legacy Documents path");
        fs::remove(roms / "vf.zip");
        std::ofstream(roms / "vf.zip", std::ios::binary) << std::string(128, 'x');
        require(!vf1_models::load((roms / "vf.zip").string(), models, error, &icons) && models.size() == expected &&
                same_icons(icons, reference_icons),
                "bad VF1 load is transactional");
        {
            auto r = romset::load((documents / "vf2.zip").string());
            require(!r.ok && !r.error.empty() && r.vf1_models.empty(), "corrupt companion is reported");
        }
        std::ofstream(roms / "vf2.zip") << "preserve existing file";
        {
            auto r = romset::import_file((source / "vf2.zip").string(), (roms / "vf2.zip").string());
            require(!r.ok, "import rejects corrupt companion");
            std::ifstream in(roms / "vf2.zip");
            std::string kept;
            std::getline(in, kept);
            require(kept == "preserve existing file", "failed import preserves old file");
        }
        require(std::distance(fs::directory_iterator(roms), fs::directory_iterator{}) == 2,
                "no extracted files, mesh cache or staging leftovers");
        std::cout << "ZIP-only startup, import, legacy path, missing/corrupt VF1 and preservation checks passed.\n";
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
