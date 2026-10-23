// Real-game face changes versus the previous, fixed-expression VF1 model.
#include "../../src/machine/model2a.h"
#include "../../src/host/input_script.h"
#include <filesystem>
#include <iostream>

static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void ppm(const std::filesystem::path &path, const bitmap_rgb32 &b)
{
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << b.width() << ' ' << b.height() << "\n255\n";
    for (int y = 0; y < b.height(); ++y) for (int x = 0; x < b.width(); ++x)
        for (int shift : {16, 8, 0}) f.put(char(b.pix(y, x) >> shift));
}

int main(int argc, char **argv) try
{
    require(argc >= 2, "usage: facecheck vf2.zip [output-directory] (vf.zip beside it)");
    auto dynamic_roms = romset::load(argv[1]);
    require(dynamic_roms.ok && dynamic_roms.vf1_models.count(0x84dc10) &&
            dynamic_roms.vf1_models.count(0x86ff68) && dynamic_roms.vf1_models.count(0x870c11), "both ROM ZIPs required");
    auto fixed_roms = dynamic_roms;
    for (u32 head : {0x86ff68, 0x870c11}) fixed_roms.vf1_models[head] = fixed_roms.vf1_models.at(0x84dc10);
    model2a dynamic(std::move(dynamic_roms)), fixed(std::move(fixed_roms));
    for (auto *m : {&dynamic, &fixed}) { m->set_sound(false); m->reset(); }
    input_script script;
    std::string error;
    require(script.load("tools/models/akira-demo.txt", error), "demo script missing");
    const std::filesystem::path out = argc > 2 ? argv[2] : "build/facecheck";
    std::filesystem::create_directories(out);
    std::set<u32> seen;
    u32 previous = 0;
    int stable = 0;
    for (int frame = 0; frame < 2400; ++frame) {
        dynamic.run_frame(); fixed.run_frame();
        require(dynamic.workram() == fixed.workram(), "expression changes altered game RAM");
        const u32 head = dynamic.peek32(0x5109c8);
        stable = head == previous ? stable + 1 : 0;
        previous = head;
        if (dynamic.game_mode() == 9 && dynamic.vf1_selected(0) && stable >= 2 &&
                (head == 0x19c || head == 0x2a8 || head == 0x2a9) && !seen.count(head)) {
            const auto &a = dynamic.render(), &b = fixed.render();
            unsigned changed = 0;
            for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x)
                changed += a.pix(y, x) != b.pix(y, x);
            require(head == 0x19c ? changed == 0 : changed > 0, "face did not follow VF2's expression");
            const std::string name = head == 0x19c ? "neutral" : head == 0x2a8 ? "teeth" : "eyes-shut";
            ppm(out / (name + ".ppm"), a);
            ppm(out / (name + "-fixed.ppm"), b);
            std::cout << "frame " << frame << ' ' << name << ": " << changed << " changed pixels\n";
            seen.insert(head);
        }
        u8 ports[input_script::N_PORTS];
        script.apply(frame, ports);
        for (auto *m : {&dynamic, &fixed}) {
            m->m_in0 = ports[0]; m->m_in1 = ports[1]; m->m_in2 = ports[2]; m->m_sw = ports[3];
        }
    }
    require(seen.size() == 3, "script did not exercise all three expressions");
    std::cout << "PASS: real VF2 face timing, unchanged neutral face and game RAM for 2400 frames\n";
    return 0;
} catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
