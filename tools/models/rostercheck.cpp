// Local-ROM regression: VF1's eight fighters use VF2's gameplay and independent skins.
#include "../../src/machine/model2a.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); }
constexpr const char *names[] = {"akira", "pai", "lau", "wolf", "jeffry", "kage", "sarah", "jacky", "shun", "lion"};
constexpr u8 fighters[] = {0, 6, 4, 7, 5, 3, 2, 1, 8, 10};
struct press { int at, length, port; u8 bits; };
struct shot {
    std::vector<u32> pixels;
    std::vector<u8> ram;
    std::array<bool, 2> selected;
    std::array<u8, 2> fighters;
};
using shots = std::map<int, shot>;
struct scenario {
    int p1 = 0, p2 = -1, choices = 0;
    bool available = true, toggle = false, away = false;
};

shots run(const romset::result &source, const scenario &s)
{
    auto roms = source;
    if (!s.available) roms.vf1_models.clear();
    model2a m(std::move(roms));
    m.set_sound(false);
    m.reset();
    const bool versus = s.p2 >= 0;
    std::vector<press> events{{1000,6,0,u8(versus ? 3 : 1)}, {1026,6,0,u8(versus ? 3 : 1)},
        {1092,6,0,u8(versus ? 0x30 : 0x10)}, {1372,4,1,1}};
    for (int i = 0; i < s.p1; ++i) events.push_back({1120+12*i,4,1,0x40});
    if (versus) {
        for (int i = 0; i < 9-s.p2; ++i) events.push_back({1120+12*i,4,2,0x80});
        events.push_back({1372,4,2,1});
    }
    for (int p = 0; p < 2; ++p) if (s.choices & (1 << p)) {
        events.push_back({1280,6,p+1,0x20});
        if (s.toggle) {
            events.push_back({1310,6,p+1,0x10});
            events.push_back({1340,6,p+1,0x20});
        }
        // A confirmed appearance cannot be changed by subsequent fight inputs.
        events.push_back({1400,6,p+1,0x10});
    }
    if (s.away) {
        events.push_back({1310,4,1,0x40});
        events.push_back({1330,4,1,0x80});
    }
    events.push_back({1700,4,1,0x10});
    events.push_back({1740,4,1,1});
    events.push_back({1770,4,1,2});
    shots result;
    for (int frame = 0; frame <= 1800; ++frame) {
        m.run_frame();
        if (frame == 1240 || frame == 1290 || frame == 1320 || frame == 1350 || frame == 1800) {
            auto &out = result[frame];
            const auto &picture = m.render();
            out.pixels.assign(picture.data(), picture.data()+picture.width()*picture.height());
            out.ram = m.workram();
            out.selected = {m.vf1_selected(0), m.vf1_selected(1)};
            for (int p = 0; p < 2; ++p) out.fighters[p] = out.ram[0x10980+0x2000*p+0x1b0];
        }
        u8 ports[3] = {0xff, 0xff, 0xff};
        for (const auto &event : events)
            if (frame >= event.at && frame < event.at+event.length) ports[event.port] &= ~event.bits;
        m.m_in0 = ports[0]; m.m_in1 = ports[1]; m.m_in2 = ports[2];
    }
    require(m.game_mode() == 9, "navigation script did not reach a fight");
    m.reset();
    require(!m.vf1_selected(0) && !m.vf1_selected(1), "reset retained a VF1 choice");
    return result;
}

void ppm(const std::filesystem::path &path, const shot &picture)
{
    std::ofstream file(path, std::ios::binary);
    require(bool(file), "cannot create screenshot: " + path.string());
    file << "P6\n" << model2a::WIDTH << ' ' << model2a::HEIGHT << "\n255\n";
    for (u32 pixel : picture.pixels) for (int shift : {16, 8, 0}) file.put(char(pixel >> shift));
}

void changed(const shot &base, const shot &skin, int fighter, bool selecting)
{
    unsigned model = 0, icon = 0;
    for (size_t i = 0; i < base.pixels.size(); ++i) if (base.pixels[i] != skin.pixels[i]) {
        const int x = i % model2a::WIDTH, y = i / model2a::WIDTH;
        if (selecting && y >= 272) {
            require(x > 8+48*fighter && x < 55+48*fighter && y > 272 && y < 359,
                    std::string(names[fighter])+": skin changed another select icon or its border");
            ++icon;
        } else ++model;
    }
    require(model > 100, std::string(names[fighter])+": model did not visibly change");
    require(!selecting || icon > 100, std::string(names[fighter])+": select icon did not visibly change");
}

void independent(const shot &base, const shot &p1, const shot &p2, const shot &both, int shared_slot)
{
    require(base.ram == p1.ram && base.ram == p2.ram && base.ram == both.ram, "independent choices changed VF2 RAM");
    unsigned changed1 = 0, changed2 = 0, shared = 0;
    for (size_t i = 0; i < base.pixels.size(); ++i) {
        const int x = i % model2a::WIDTH, y = i / model2a::WIDTH;
        if (shared_slot >= 0 && p1.pixels[i] != base.pixels[i] && p2.pixels[i] != base.pixels[i]) {
            require(x > 8+48*shared_slot && x < 55+48*shared_slot && y > 272 && y < 359 &&
                    p1.pixels[i] == p2.pixels[i] && both.pixels[i] == p1.pixels[i], "mirror skin altered both players' models");
            ++shared;
        } else if (p1.pixels[i] != base.pixels[i]) {
            ++changed1;
            require(p2.pixels[i] == base.pixels[i] && both.pixels[i] == p1.pixels[i], "P1 skin altered P2");
        } else {
            changed2 += p2.pixels[i] != base.pixels[i];
            require(both.pixels[i] == p2.pixels[i], "combined skins lost an independent image");
        }
    }
    require(changed1 > 100 && changed2 > 100, "a player's selected model did not change");
    require(shared_slot < 0 || shared > 100, "shared mirror icon did not follow either player");
}
}

int main(int argc, char **argv) try
{
    require(argc >= 2, "usage: rostercheck vf2.zip [output-directory] (vf.zip beside it)");
    const auto source = romset::load(argv[1]);
    require(source.ok && !source.vf1_models.empty(), "both valid ROM ZIPs are required: " + source.error);
    const std::filesystem::path output = argc > 2 ? argv[2] : "build/rostercheck";
    std::filesystem::create_directories(output);
    for (int slot = 0; slot < 8; ++slot) {
        const auto original = run(source, {slot, -1, 0, false});
        const auto chosen = run(source, {slot, -1, 1, true, true});
        for (const auto &[frame, shot] : chosen) {
            require(shot.ram == original.at(frame).ram, std::string(names[slot])+": skin inputs altered VF2 RAM at " + std::to_string(frame));
            require(shot.fighters[0] % 13 == fighters[slot], std::string(names[slot])+": wrong fighter chosen");
            const bool enabled = frame == 1290 || frame == 1350 || frame == 1800;
            require(shot.selected[0] == enabled && !shot.selected[1], std::string(names[slot])+": selection did not follow up/down/confirmation");
            if (enabled) changed(original.at(frame), shot, slot, frame != 1800);
            else require(shot.pixels == original.at(frame).pixels, std::string(names[slot])+": default/down did not restore VF2");
        }
        for (const auto &[frame, label] : {std::pair{1350, "select"}, std::pair{1800, "fight"}}) {
            ppm(output / (std::string(names[slot])+"-"+label+"-vf1.ppm"), chosen.at(frame));
            ppm(output / (std::string(names[slot])+"-"+label+"-vf2.ppm"), original.at(frame));
        }
        std::cout << "PASS: " << names[slot] << " model, original icon, up/down, fight, unchanged VF2 RAM\n" << std::flush;
    }
    for (int slot : {8, 9}) {
        const auto original = run(source, {slot, -1, 1, false, true});
        const auto unchanged = run(source, {slot, -1, 1, true, true});
        for (const auto &[frame, shot] : unchanged)
            require(shot.pixels == original.at(frame).pixels && shot.ram == original.at(frame).ram && !shot.selected[0],
                    std::string(names[slot])+": unsupported fighter changed");
    }
    const auto away = run(source, {2, -1, 1, true, false, true});
    require(!away.at(1350).selected[0] && !away.at(1800).selected[0], "browsing between supported fighters retained VF1");
    const auto missing = run(source, {1, -1, 1, false});
    require(!missing.at(1290).selected[0] && !missing.at(1800).selected[0], "missing VF1 enabled a skin");
    std::cout << "PASS: Shun/Lion unchanged, browsing away clears choice, missing companion and reset\n" << std::flush;
    for (const auto &[first, second] : {std::pair{1, 6}, std::pair{3, 3}, std::pair{5, 5}, std::pair{4, 4}}) {
        const auto base = run(source, {first, second});
        const auto p1 = run(source, {first, second, 1});
        const auto p2 = run(source, {first, second, 2});
        const auto both = run(source, {first, second, 3});
        for (const auto &[frame, label] : {std::pair{1350, "select"}, std::pair{1800, "fight"}}) {
            const auto prefix = std::string(names[first])+"-"+names[second]+"-"+label;
            ppm(output / (prefix+"-vf2.ppm"), base.at(frame));
            ppm(output / (prefix+"-p1.ppm"), p1.at(frame));
            ppm(output / (prefix+"-p2.ppm"), p2.at(frame));
            ppm(output / (prefix+".ppm"), both.at(frame));
        }
        independent(base.at(1350), p1.at(1350), p2.at(1350), both.at(1350), first == second ? first : -1);
        independent(base.at(1800), p1.at(1800), p2.at(1800), both.at(1800), -1);
        require(p1.at(1800).selected == std::array<bool,2>{true,false} &&
                p2.at(1800).selected == std::array<bool,2>{false,true} &&
                both.at(1800).selected == std::array<bool,2>{true,true}, "versus selections not retained");
        std::cout << "PASS: independent " << names[first] << '/' << names[second] << " selections and fight models\n" << std::flush;
    }
    return 0;
} catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
