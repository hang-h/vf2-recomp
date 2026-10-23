// Local model experiments, prepared from vf.zip at startup or from a diagnostic mesh file.
// Each mesh uses one VF2 object's local coordinates and current animation matrix.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

class model_overrides
{
public:
    struct triangle {
        uint32_t colour;
        std::array<std::array<float, 3>, 3> points;
    };
    using mesh = std::vector<triangle>;
    using table = std::map<uint32_t, mesh>;

    void assign(table &&prepared) { objects = std::move(prepared); }

    // Transactional: a malformed file never leaves a partially active replacement.
    bool load(const std::string &path, std::string &error)
    {
        std::ifstream in(path);
        std::string magic;
        unsigned version = 0, count = 0;
        std::map<uint32_t, mesh> parsed;
        size_t total = 0;
        auto bad = [&] { error = "invalid or unsupported VF2MESH file: " + path; return false; };
        if (!(in >> magic >> version >> count) || magic != "VF2MESH" || version != 1 || !count || count > 128) return bad();
        for (unsigned i = 0; i < count; ++i) {
            uint32_t address;
            unsigned faces;
            if (!(in >> std::hex >> address >> std::dec >> faces) || !faces || faces > 16384 ||
                (total += faces) > 65536 || parsed.count(address)) return bad();
            // Only ROM objects: never accidentally replace mutable stage/effect RAM.
            if (!(address & 0x800000) || address >= 0x1000000) return bad();
            mesh &m = parsed[address];
            m.resize(faces);
            for (triangle &t : m) {
                if (!(in >> std::hex >> t.colour >> std::dec) || t.colour > 0xffffff) return bad();
                t.colour |= 0xff000000;
                for (auto &p : t.points) for (float &v : p)
                    if (!(in >> v) || !std::isfinite(v) || std::abs(v) > 1000000) return bad();
            }
        }
        in >> std::ws;
        if (!in.eof()) return bad();
        objects.swap(parsed);
        error.clear();
        return true;
    }

    const mesh *find(uint32_t address) const
    {
        const auto i = objects.find(address);
        return i == objects.end() ? nullptr : &i->second;
    }
    size_t size() const { return objects.size(); }

    void configure_trace(const char *spec)
    {
        if (!spec) return;
        char *end;
        const auto frame = strtoull(spec, &end, 10);
        if (end == spec) return;
        auto last = frame;
        if (*end == '-') {
            const char *begin = end + 1;
            last = strtoull(begin, &end, 10);
            if (end == begin || last < frame) return;
        }
        if (*end != ':' || !end[1]) return;
        trace_frame = frame;
        trace_last = last;
        trace_path = end + 1;
    }
    void trace(uint64_t frame, uint32_t object, uint32_t count, uint32_t mode,
               const float *matrix, float fx, float fy)
    {
        if (frame < trace_frame || frame > trace_last || trace_path.empty()) return;
        if (!trace_opened) { trace_file.open(trace_path); trace_opened = true; }
        if (!trace_file) return;
        trace_file << "{\"frame\":" << frame << ",\"object\":\"0x" << std::hex << object << std::dec
                   << "\",\"count\":" << count << ",\"mode\":" << mode << ",\"matrix\":[";
        trace_file.precision(9);
        for (int k = 0; k < 12; ++k) trace_file << (k ? "," : "") << matrix[k];
        trace_file << "],\"focus\":[" << fx << ',' << fy << "]}\n";
        trace_file.flush();
    }

private:
    std::map<uint32_t, mesh> objects;
    uint64_t trace_frame = std::numeric_limits<uint64_t>::max();
    uint64_t trace_last = 0;
    std::string trace_path;
    std::ofstream trace_file;
    bool trace_opened = false;
};
