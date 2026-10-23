// Real-ROM regression: cabinet inputs, select previews, confirmation and mirror matches.
#include "../../src/machine/model2a.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct press { int at, length, port; u8 bits; };
struct shot { std::vector<u32> pixels; std::vector<u8> ram; std::array<bool, 2> selected; };
using shots = std::map<int, shot>;

shots run(const romset::result &source, bool available, bool mirror, int choice, bool toggle = false, bool away = false)
{
    auto roms = source;
    if (!available) roms.vf1_models.clear();
    model2a m(std::move(roms));
    require(m.vf1_models_available() == available, "model availability");
    m.set_sound(false);
    m.reset();
    std::vector<press> events{{1000, 6, 0, u8(mirror ? 3 : 1)}, {1026, 6, 0, u8(mirror ? 3 : 1)},
        {1092, 6, 0, u8(mirror ? 0x30 : 0x10)}, {1272, 4, 1, 1}};
    if (mirror) {
        // P2 starts on Lion; nine separate left presses choose Akira's alternate costume.
        for (int i = 0; i < 9; ++i) events.push_back({1120 + 12*i, 4, 2, 0x80});
        events.push_back({1272, 4, 2, 1});
    }
    for (int p = 0; p < 2; ++p)
        if (choice & (1 << p)) events.push_back({mirror ? 1240 : 1200, 6, p + 1, 0x20});
    if (toggle) {
        events.push_back({1230, 6, 1, 0x10});
        events.push_back({1250, 6, 1, 0x20});
        events.push_back({1300, 6, 1, 0x10}); // after confirmation: must not change the choice
    }
    if (away) {
        events.push_back({1230, 4, 1, 0x40});
        events.push_back({1250, 4, 1, 0x80});
    }
    // In a fight these still reach VF2, and never change the chosen appearance.
    events.push_back({1600, 4, 1, 0x10});
    shots result;
    for (int frame = 0; frame <= 1700; ++frame) {
        m.run_frame();
        if (frame == 1180 || frame == 1210 || frame == 1240 || frame == 1250 || frame == 1260 || frame == 1700) {
            auto &s = result[frame];
            const auto &b = m.render();
            s.pixels.assign(b.data(), b.data() + b.width()*b.height());
            s.ram = m.workram();
            s.selected = {m.vf1_selected(0), m.vf1_selected(1)};
        }
        u8 ports[3] = {0xff, 0xff, 0xff};
        for (const auto &e : events)
            if (frame >= e.at && frame < e.at + e.length) ports[e.port] &= ~e.bits;
        m.m_in0 = ports[0]; m.m_in1 = ports[1]; m.m_in2 = ports[2];
    }
    require(m.game_mode() == 9, "script did not reach a fight");
    m.reset();
    require(!m.vf1_selected(0) && !m.vf1_selected(1), "reset retained a previous selection");
    return result;
}

void independent(const shot &base, const shot &p1, const shot &p2, const shot &both, bool shared_icon = false)
{
    require(base.ram == p1.ram && base.ram == p2.ram && base.ram == both.ram, "skin inputs changed game RAM");
    unsigned changed1 = 0, changed2 = 0, shared = 0;
    for (size_t i = 0; i < base.pixels.size(); ++i) {
        if (shared_icon && p1.pixels[i] != base.pixels[i] && p2.pixels[i] != base.pixels[i]) {
            const int x = i % model2a::WIDTH, y = i / model2a::WIDTH;
            require(x > 8 && x < 55 && y > 272 && y < 359 &&
                    p1.pixels[i] == p2.pixels[i] && both.pixels[i] == p1.pixels[i], "unexpected shared portrait pixels");
            ++shared;
            continue;
        }
        if (p1.pixels[i] != base.pixels[i]) {
            ++changed1;
            require(p2.pixels[i] == base.pixels[i] && both.pixels[i] == p1.pixels[i], "P1 changed P2's image");
        } else {
            if (p2.pixels[i] != base.pixels[i]) ++changed2;
            require(both.pixels[i] == p2.pixels[i], "combined choices don't preserve independent images");
        }
    }
    require(changed1 > 100 && changed2 > 100, "a selected fighter did not change appearance");
    require(!shared_icon || shared > 100, "shared select icon did not follow either player");
}

void icon_check(const romset::result &source)
{
    auto with_icon = source, without_icon = source;
    without_icon.vf1_portraits = {};
    model2a shown(std::move(with_icon)), original(std::move(without_icon));
    for (auto *m : {&shown, &original}) { m->set_sound(false); m->reset(); m->set_smooth(true); }
    const press events[] = {{1000,6,0,1},{1026,6,0,1},{1092,6,0,0x10},
        {1200,6,1,0x20},{1230,6,1,0x10},{1250,6,1,0x20},{1272,4,1,1}};
    std::shared_ptr<const model2a::picture_desc> saved_on, saved_off;
    std::vector<u32> pixels_on, pixels_off;
    for (int frame = 0; frame <= 1700; ++frame) {
        shown.run_frame(); original.run_frame();
        if (frame == 1180 || frame == 1210 || frame == 1240 || frame == 1260 || frame == 1700) {
            require(shown.workram() == original.workram() && shown.tiles().tile_ram == original.tiles().tile_ram &&
                    shown.tiles().char_ram == original.tiles().char_ram, "portrait modified board RAM");
            const auto &a = shown.render(), &b = original.render();
            unsigned changed = 0;
            for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x)
                if (a.pix(y,x) != b.pix(y,x)) {
                    require(x > 8 && x < 55 && y > 272 && y < 359, "portrait changed another image or its border");
                    ++changed;
                }
            require(frame == 1210 || frame == 1260 ? changed > 100 : changed == 0,
                    "portrait did not follow up/down or appeared outside the select");
            if (frame == 1210) {
                saved_on = shown.last_picture();
                pixels_on.assign(a.data(), a.data() + a.width()*a.height());
            }
            if (frame == 1240) {
                saved_off = shown.last_picture();
                pixels_off.assign(a.data(), a.data() + a.width()*a.height());
                const auto &past = shown.redraw(*saved_on);
                for (int y = 272; y < 360; ++y) for (int x = 8; x < 56; ++x)
                    require(past.pix(y,x) == pixels_on[y*past.width()+x], "old VF1 icon redrew as VF2");
            }
            if (frame == 1260) {
                const auto &past = shown.redraw(*saved_off);
                for (int y = 272; y < 360; ++y) for (int x = 8; x < 56; ++x)
                    require(past.pix(y,x) == pixels_off[y*past.width()+x], "old VF2 icon redrew as VF1");
                std::vector<u32> gpu_layer(496*384);
                model2a::picture_parts parts;
                require(shown.render_parts(-1, gpu_layer.data(), 496, false, parts), "split rendering unavailable");
                const auto &whole = shown.render();
                for (int y = 272; y < 360; ++y) for (int x = 8; x < 56; ++x)
                    require(parts.top[y*parts.w+x] == whole.pix(y,x), "GPU composition lost portrait or cursor");
            }
        }
        u8 ports[3] = {0xff,0xff,0xff};
        for (const auto &e : events)
            if (frame >= e.at && frame < e.at + e.length) ports[e.port] &= ~e.bits;
        for (auto *m : {&shown, &original}) { m->m_in0=ports[0]; m->m_in1=ports[1]; m->m_in2=ports[2]; }
    }
    std::cout << "PASS: original VF1 select icon, restoration, board RAM, stored pictures and GPU composition\n";
}
}

int main(int argc, char **argv) try
{
    if (argc != 2) { std::cerr << "usage: selectcheck /path/to/vf2.zip (vf.zip beside it)\n"; return 2; }
    const auto source = romset::load(argv[1]);
    require(source.ok && !source.vf1_models.empty(), "both valid ROM ZIPs are required");
    const auto original = run(source, false, false, 0);
    const auto normal = run(source, true, false, 0);
    for (const auto &[f, s] : normal)
        require(s.pixels == original.at(f).pixels && s.ram == original.at(f).ram, "default is not original VF2");
    const auto chosen = run(source, true, false, 1, true);
    for (int frame : {1180, 1240}) require(chosen.at(frame).pixels == normal.at(frame).pixels, "down did not restore VF2");
    for (int frame : {1210, 1260, 1700}) {
        require(chosen.at(frame).selected[0] && chosen.at(frame).pixels != normal.at(frame).pixels, "VF1 choice lost");
        require(chosen.at(frame).ram == normal.at(frame).ram, "skin choice changed VF2 RAM");
    }
    const auto away = run(source, true, false, 1, false, true);
    require(!away.at(1260).selected[0] && !away.at(1700).selected[0], "browsing away retained VF1 choice");
    const auto missing = run(source, false, false, 1);
    require(!missing.at(1210).selected[0], "missing vf.zip enabled a skin");
    std::cout << "PASS: default VF2, up/down, confirmation, fight inputs, browsing away, missing VF1 and reset\n";
    const auto base = run(source, true, true, 0);
    const auto p1 = run(source, true, true, 1);
    const auto p2 = run(source, true, true, 2);
    const auto both = run(source, true, true, 3);
    independent(base.at(1260), p1.at(1260), p2.at(1260), both.at(1260), true);
    independent(base.at(1700), p1.at(1700), p2.at(1700), both.at(1700));
    require(p1.at(1700).selected == std::array<bool,2>{true,false} &&
            p2.at(1700).selected == std::array<bool,2>{false,true} &&
            both.at(1700).selected == std::array<bool,2>{true,true}, "mirror match choices not retained");
    std::cout << "PASS: independent P1/P2 portraits and fight models, including alternate Akira\n";
    icon_check(source);
    return 0;
} catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
