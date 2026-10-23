// A ROM-free integration check: real display-list commands, CPU pixels, GPU export,
// changing part matrices, near clipping, and a normal arcade object after a replacement.
#include "../../src/machine/model2_video.h"
#include "../../src/host/vf1_models.h"
#include "../../src/gpu/gpu3d.h"
#include <bit>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static uint32_t bits(float f) { return std::bit_cast<uint32_t>(f); }

struct scene {
    std::vector<uint32_t> buffer = std::vector<uint32_t>(0x8000), tex0 = std::vector<uint32_t>(0x80000), tex1 = tex0;
    std::vector<uint8_t> luma = std::vector<uint8_t>(0x8000), gamma = std::vector<uint8_t>(256);
    std::vector<uint16_t> pal = std::vector<uint16_t>(0x2000), xlat = std::vector<uint16_t>(0x6000), tex = std::vector<uint16_t>(256);
    std::vector<uint32_t> rom = std::vector<uint32_t>(256);
    std::unique_ptr<model2_video> video;
    uint64_t frame = 0;
    scene(model_overrides::table prepared = {}, size_t rom_words = 256) {
        rom.resize(rom_words);
        for (int i = 0; i < 256; ++i) gamma[i] = i;
        for (int i = 0; i < 0x6000; ++i) xlat[i] = ((i >> 8) & 31) * 255 / 31;
        pal[0x1000] = 31; // original model is red
        // normals-present triangle, two sided, min-z sort, linked
        const uint32_t words[] = {bits(-20), bits(-20), bits(0), bits(20), bits(-20), bits(0),
            2 | 0x100 | 0x400 | 0x20000, bits(0), bits(0), bits(1), bits(0), bits(20), bits(0),
            0, 0, 0, 0};
        std::copy(std::begin(words), std::end(words), rom.begin());
        video = std::make_unique<model2_video>(buffer.data(), tex0.data(), tex1.data(), luma.data(), pal.data(),
            xlat.data(), gamma.data(), reinterpret_cast<uint8_t *>(tex.data()), tex.size()*2,
            reinterpret_cast<uint8_t *>(rom.data()), rom.size()*4, std::move(prepared));
        video->set_delay(0);
    }
    std::vector<gpu3d_poly> draw(float x, float z, uint32_t object = 0x800000, float angle = 0, float scale = 1, bool pair = false) {
        size_t at = 0;
        auto word = [&](uint32_t v) { buffer[at++] = v; };
        auto cmd = [&](int c) { word(uint32_t(c) << 23); };
        cmd(3); word(0); word(495u << 16 | 383);
        for (int i = 0; i < 4; ++i) word(248u << 16 | 192);
        cmd(9); word(bits(300)); word(bits(300));
        cmd(10); word(bits(0)); word(bits(0)); word(bits(1));
        auto add_object = [&](float position) {
            cmd(11);
            const float c = cos(angle)*scale, s = sin(angle)*scale;
            const float matrix[] = {c,0,-s, 0,scale,0, s,0,c, position,0,z};
            for (float f : matrix) word(bits(f));
            cmd(1); word(0); word(0); word(object); word(1);
        };
        add_object(x);
        if (pair) add_object(x + 80);
        cmd(15);
        video->m_geo_frame = ++frame;
        video->geo_parse(); video->frame_end(false, frame);
        std::vector<gpu3d_poly> out;
        video->export_shown(out);
        return out;
    }
};

static void ppm(const std::filesystem::path &path, const bitmap_rgb32 &picture) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << picture.width() << ' ' << picture.height() << "\n255\n";
    for (int y = 0; y < picture.height(); ++y) for (int x = 0; x < picture.width(); ++x) {
        uint32_t c = picture.pix(y, x);
        for (int shift : {16, 8, 0}) file.put(char(c >> shift));
    }
}

int main(int argc, char **argv) try {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "build/modelcheck";
    std::filesystem::create_directories(dir);
    const auto good = dir / "test.mesh", bad = dir / "invalid.mesh";
    { std::ofstream f(good); f << "VF2MESH 1 1\n800000 1\n00ff00 -20 -20 0 20 -20 0 0 20 0\n"; }
    { std::ofstream f(bad); f << "VF2MESH 1 1\n800000 1\nff00ff nan 0 0\n"; }
    model_overrides loader;
    std::string error;
    require(loader.load(good.string(), error), "valid package rejected");
    require(!loader.load(bad.string(), error) && loader.size() == 1, "invalid load changed active data");
    setenv("VF2_MODEL_OVERRIDES", good.c_str(), 1);
    scene test;
    auto first = test.draw(0, 200);
    require(first.size() == 1 && first[0].colour == 0xff00ff00, "replacement not green");
    require(test.video->layer()->pix(192, 248) == 0xff00ff00, "CPU did not draw replacement");
    auto moved = test.draw(40, 200);
    require(moved.size() == 1 && std::abs(moved[0].x[0] - first[0].x[0] - 60) < 0.01f, "VF2 translation not applied");
    auto rotated = test.draw(0, 200, 0x800000, 0.6f);
    require(rotated.size() == 1 && std::abs(rotated[0].x[0] - first[0].x[0]) > 1, "VF2 rotation not applied");
#ifdef VF2_MODELCHECK_METAL
    auto gpu = gpu3d_create_metal();
    require(bool(gpu), "Metal unavailable");
    gpu3d_rams rams; test.video->gpu_rams(rams); gpu->set_rams(rams);
    gpu->set_quality(gpu3d_quality::exact);
    std::vector<uint32_t> pixels(496*384);
    require(gpu->draw(rotated.data(), rotated.size(), 496, 384, 1, nullptr, 0, pixels.data(), 496), "Metal draw failed");
    auto cpu = test.video->layer();
    for (int y = 0; y < 384; ++y) for (int x = 0; x < 496; ++x)
        require(pixels[y*496+x] == cpu->pix(y,x), "CPU/Metal pixel mismatch");
#endif
    require(test.draw(0, -200).empty(), "behind-camera mesh was not clipped");
    auto clipped = test.draw(0, 5, 0x800000, 0.6f);
    require(!clipped.empty(), "near-plane crossing disappeared");
    for (const auto &p : clipped) for (int i = 0; i < p.n; ++i)
        require(std::isfinite(p.x[i]) && std::isfinite(p.y[i]) && p.n <= 8, "invalid clipped projection");
    auto original = test.draw(0, 200, 0x800100); // masked ROM offset 0; unbound address
    require(original.size() == 1 && original[0].colour == 0xffff0000, "normal model inherited override colour/state");
    scene smooth;
    smooth.video->set_smooth(true);
    auto older = smooth.draw(0, 200);
    auto a = smooth.video->shown_list();
    smooth.draw(10, 200);
    auto b = smooth.video->shown_list();
    const bitmap_rgb32 *map = nullptr;
    int picture = smooth.video->replay_layer({a, b, 0.5f}, map);
    std::vector<gpu3d_poly> between;
    smooth.video->export_picture(picture, between);
    require(between.size() == 1 && std::abs(between[0].x[0] - older[0].x[0] - 7.5f) < 0.01f,
        "replacement did not use VF2's blended matrix");
    unsetenv("VF2_MODEL_OVERRIDES");
    scene baseline;
    auto normal = baseline.draw(0, 200);
    require(normal.size() == 1 && normal[0].colour == original[0].colour, "disabled override changed normal drawing");
    model_overrides::table prepared;
    prepared[0x84dc10] = *loader.find(0x800000);
    prepared[0x98c21c] = *loader.find(0x800000);
    prepared[0x86ff68] = *loader.find(0x800000);
    prepared[0x86ff68][0].colour = 0xff00ffff;
    prepared[0x870c11] = *loader.find(0x800000);
    prepared[0x870c11][0].colour = 0xff0000ff;
    scene selected(std::move(prepared));
    // Give the synthetic ROM a red original at each face address as well.
    for (size_t offset : {0x68, 0x11})
        std::copy_n(selected.rom.begin(), 17, selected.rom.begin() + offset);
    model2_video::model_selection selection;
    selection.selecting = true;
    selection.humans = 3;
    selection.parts[0][2] = selection.parts[1][2] = 0x800000;
    selection.vf1 = {true, false};
    selected.video->set_model_selection(selection);
    selected.draw(-40, 200, 0x800000, 0, 1, true);
    require(selected.video->layer()->pix(192,188) == 0xff00ff00 &&
            selected.video->layer()->pix(192,308) == 0xffff0000, "P1 selection changed P2's identical portrait");
    selection.vf1 = {false, true};
    selected.video->set_model_selection(selection);
    selected.draw(-40, 200, 0x800000, 0, 1, true);
    require(selected.video->layer()->pix(192,188) == 0xffff0000 &&
            selected.video->layer()->pix(192,308) == 0xff00ff00, "P2 selection changed P1's identical portrait");
    selection.selecting = false;
    selection.parts[0][0] = selection.parts[1][0] = 0x800000;
    selected.video->set_model_selection(selection);
    selected.draw(-40, 200, 0x800000, 0, 1, true);
    require(selected.video->layer()->pix(192,188) == 0xffff0000 &&
            selected.video->layer()->pix(192,308) == 0xff00ff00, "mirror-match root ownership lost");
    // The actor's slot may be the default or the new expression. In both
    // cases the face drawn must follow the display list's actual object.
    selection.selecting = false;
    selection.vf1 = {true, false};
    for (u32 face : {0x86ff68u, 0x870c11u}) {
        selection.parts[0][0] = selection.parts[1][0] = face;
        selection.parts[0][2] = face;
        selected.video->set_model_selection(selection);
        selected.draw(-40, 200, face, 0, 1, true);
        require(selected.video->layer()->pix(192,188) == (face == 0x86ff68 ? 0xff00ffff : 0xff0000ff) &&
                selected.video->layer()->pix(192,308) == 0xffff0000, "face swap lost expression or affected the opponent");
        selection.parts[0][2] = 0x84dc10;
        selected.video->set_model_selection(selection);
        selected.draw(0, 200, face);
        require(selected.video->layer()->pix(192,248) == (face == 0x86ff68 ? 0xff00ffff : 0xff0000ff),
                "face used actor slot instead of the displayed expression");
    }
    selection.parts[0][2] = selection.parts[1][2] = 0x800000;
    selected.video->set_smooth(true);
    selection.selecting = true;
    selection.humans = 1;
    selection.vf1 = {false, false};
    selected.video->set_model_selection(selection);
    selected.draw(0, 200);
    auto before_choice = selected.video->shown_list();
    selection.vf1[0] = true;
    selected.video->set_model_selection(selection);
    selected.draw(10, 200);
    auto after_choice = selected.video->shown_list();
    selection.vf1[0] = false;
    selected.video->set_model_selection(selection);
    picture = selected.video->replay_layer({before_choice, after_choice, 0.5f}, map);
    selected.video->export_picture(picture, between);
    require(between.size() == 1 && between[0].colour == 0xff00ff00, "replay used live skin instead of captured choice");
    picture = selected.video->replay_layer({before_choice, before_choice, 1.0f}, map);
    selected.video->export_picture(picture, between);
    require(between.size() == 1 && between[0].colour == 0xffff0000, "old picture changed skin retroactively");
    selection.vf1[0] = true;
    selection.parts[0][2] = 0x86ff68;
    selected.video->set_model_selection(selection);
    selected.draw(0, 200, 0x86ff68);
    auto tense_face = selected.video->shown_list();
    selection.parts[0][2] = 0x870c11;
    selected.video->set_model_selection(selection);
    selected.draw(10, 200, 0x870c11);
    auto shut_face = selected.video->shown_list();
    picture = selected.video->replay_layer({tense_face, shut_face, 0.5f}, map);
    selected.video->export_picture(picture, between);
    require(between.size() == 1 && between[0].colour == 0xff0000ff, "smooth replay lost a face change");
    picture = selected.video->replay_layer({tense_face, tense_face, 1.0f}, map);
    selected.video->export_picture(picture, between);
    require(between.size() == 1 && between[0].colour == 0xff00ffff, "old face changed retroactively");
    // New roster bindings must follow the display list's actual expression,
    // even when the actor's current head is neutral or has advanced a frame.
    model_overrides::table roster_models;
    for (int slot : {1,2,16,17}) {
        auto &mesh = roster_models[vf1_models::key(1, slot)];
        mesh = *loader.find(0x800000);
        mesh[0].colour = slot == 16 ? 0xff00ffff : slot == 17 ? 0xff0000ff : 0xff00ff00;
    }
    roster_models[vf1_models::key(6, 1)] = *loader.find(0x800000);
    roster_models[vf1_models::key(6, 1)][0].colour = 0xffff00ff;
    scene roster(std::move(roster_models), 512);
    const std::vector<u32> original_triangle(roster.rom.begin(), roster.rom.begin()+17);
    selection = {};
    selection.selecting = true;
    selection.humans = 3;
    selection.fighter = {1,1}; // Pai mirror, with only P1 using VF1
    selection.vf1 = {true,false};
    selection.parts[0][2] = selection.parts[1][2] = 0x813aec;
    for (u32 face : {0x813aecu, 0x87a23eu, 0x87acf3u}) {
        std::copy(original_triangle.begin(), original_triangle.end(), roster.rom.begin()+(face & 511));
        roster.video->set_model_selection(selection);
        roster.draw(-40, 200, face, 0, 1, true);
        const u32 colour = face == 0x87a23e ? 0xff00ffff : face == 0x87acf3 ? 0xff0000ff : 0xff00ff00;
        require(roster.video->layer()->pix(192,188) == colour &&
                roster.video->layer()->pix(192,308) == 0xffff0000,
                "Pai's displayed expression lost its roster binding or changed P2");
    }
    // Reuse an unchanged object address across two captured fighters: the
    // replay must use its saved roster index, not the current menu choice.
    std::copy(original_triangle.begin(), original_triangle.end(), roster.rom.begin()+0x80);
    selection.humans = 1;
    selection.parts[0][1] = selection.parts[0][2] = 0x800080;
    roster.video->set_smooth(true);
    roster.video->set_model_selection(selection);
    roster.draw(0, 200, 0x800080);
    auto pai_picture = roster.video->shown_list();
    selection.fighter[0] = 6;
    roster.video->set_model_selection(selection);
    roster.draw(10, 200, 0x800080);
    auto sarah_picture = roster.video->shown_list();
    selection.fighter[0] = 1;
    roster.video->set_model_selection(selection);
    picture = roster.video->replay_layer({pai_picture, sarah_picture, 0.5f}, map);
    roster.video->export_picture(picture, between);
    require(between.size() == 1 && between[0].colour == 0xffff00ff, "replay used the live fighter instead of saved Sarah");
    picture = roster.video->replay_layer({pai_picture, pai_picture, 1.0f}, map);
    roster.video->export_picture(picture, between);
    require(between.size() == 1 && between[0].colour == 0xff00ff00, "saved Pai changed to a different fighter");
    if (argc > 2) {
        setenv("VF2_MODEL_OVERRIDES", argv[2], 1);
        scene preview;
        for (int k = 0; k < 3; ++k) {
            auto polygons = preview.draw(0, 200, 0x800000, k*0.7f, 600);
            require(!polygons.empty(), "preview package produced no polygons");
#ifdef VF2_MODELCHECK_METAL
            preview.video->gpu_rams(rams); gpu->set_rams(rams);
            require(gpu->draw(polygons.data(), polygons.size(), 496, 384, 1, nullptr, 0, pixels.data(), 496), "preview Metal draw failed");
            auto cpu = preview.video->layer();
            for (int y = 0; y < 384; ++y) for (int x = 0; x < 496; ++x)
                require(pixels[y*496+x] == cpu->pix(y,x), "preview CPU/Metal pixel mismatch");
#endif
            ppm(dir / ("preview-" + std::to_string(k) + ".ppm"), *preview.video->layer());
        }
    }
    std::cout << "PASS: package validation, VF2 matrices/smoothing, clipping, CPU/GPU export, fallback, per-player selection, roster expressions and replayed skins\n";
    return 0;
} catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
