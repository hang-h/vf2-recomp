#include "romset.h"
#include "rom_zip.h"
#include "vf1_models.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <zlib.h>

namespace romset {

namespace {

using rom_zip::zip;
using rom_zip::entry;

// ---- the set, as MAME's ROM_START( vf2 ) with MODEL2_CPU_BOARD and MODEL2A_VID_BOARD ----

struct region_def { const char *name; size_t size; uint8_t fill; };
const region_def REGIONS[N_REGIONS] = {
	{ "maincpu", 0x200000, 0 }, { "main_data", 0x2400000, 0 }, { "copro_data", 0x800000, 0 },
	{ "polygons", 0x2000000, 0 }, { "textures", 0x1000000, 0xff }, { "audiocpu", 0x80000, 0 },
	{ "samples", 0x800000, 0 }, { "copro_tgp_tables", 0x40000, 0 }, { "other_data", 0x80000, 0 },
	{ "video_unk", 0x200000, 0 },
};
// ROM_LOAD32_WORD (16-bit halves of 32-bit words), ROM_LOAD16_WORD_SWAP, ROM_LOAD32_BYTE
enum kind { WORD32, WORDSWAP, BYTE32 };
struct rom { const char *name; uint32_t crc; region_id region; uint32_t offset; kind k; };
const rom ROMS[] = {
	{ "epr-18385.12", 0x78ed2d41, MAINCPU, 0x000000, WORD32 },
	{ "epr-18386.13", 0x3418f428, MAINCPU, 0x000002, WORD32 },
	{ "epr-18387.14", 0x124a8453, MAINCPU, 0x040000, WORD32 },
	{ "epr-18388.15", 0x8d347980, MAINCPU, 0x040002, WORD32 },
	{ "mpr-17560.10", 0xd1389864, MAIN_DATA, 0x000000, WORD32 },
	{ "mpr-17561.11", 0xb98d0101, MAIN_DATA, 0x000002, WORD32 },
	{ "mpr-17558.8", 0x4b15f5a6, MAIN_DATA, 0x400000, WORD32 },
	{ "mpr-17559.9", 0xd3264de6, MAIN_DATA, 0x400002, WORD32 },
	{ "mpr-17566.6", 0xfb41ef98, MAIN_DATA, 0x800000, WORD32 },
	{ "mpr-17567.7", 0xc3396922, MAIN_DATA, 0x800002, WORD32 },
	{ "mpr-17564.4", 0xd8062489, MAIN_DATA, 0xc00000, WORD32 },
	{ "mpr-17565.5", 0x0517c6e9, MAIN_DATA, 0xc00002, WORD32 },
	{ "mpr-17554.16", 0x27896d82, POLYGONS, 0x000000, WORD32 },
	{ "mpr-17548.20", 0xc95facc2, POLYGONS, 0x000002, WORD32 },
	{ "mpr-17555.17", 0x4df2810b, POLYGONS, 0x400000, WORD32 },
	{ "mpr-17549.21", 0xe0bce0e6, POLYGONS, 0x400002, WORD32 },
	{ "mpr-17556.18", 0x41a47616, POLYGONS, 0x800000, WORD32 },
	{ "mpr-17550.22", 0xc36ff3f5, POLYGONS, 0x800002, WORD32 },
	{ "mpr-17553.25", 0x5da1c5d3, TEXTURES, 0x000000, WORD32 },
	{ "mpr-17552.24", 0xe91e7427, TEXTURES, 0x000002, WORD32 },
	{ "mpr-17547.27", 0xbe940431, TEXTURES, 0x800000, WORD32 },
	{ "mpr-17546.26", 0x042a194b, TEXTURES, 0x800002, WORD32 },
	{ "epr-17574.30", 0x4d4c3a55, AUDIOCPU, 0x000000, WORDSWAP },
	{ "mpr-17573.31", 0xe43557fe, SAMPLES, 0x000000, WORDSWAP },
	{ "mpr-17572.32", 0x4febecc8, SAMPLES, 0x200000, WORDSWAP },
	{ "mpr-17571.36", 0x51caa584, SAMPLES, 0x400000, WORDSWAP },
	{ "mpr-17570.37", 0xbccd324b, SAMPLES, 0x600000, WORDSWAP },
	{ "opr-14742a.45", 0x90c6b117, COPRO_TABLES, 0x000000, WORD32 },
	{ "opr-14743a.46", 0xae7f446b, COPRO_TABLES, 0x000002, WORD32 },
	{ "opr-14744.58", 0x730ea9e0, OTHER_DATA, 0x000000, WORD32 },
	{ "opr-14745.59", 0x4c934d96, OTHER_DATA, 0x000002, WORD32 },
	{ "opr-14746.62", 0x2a266cbd, OTHER_DATA, 0x040000, WORD32 },
	{ "opr-14747.63", 0xa4ad5e19, OTHER_DATA, 0x040002, WORD32 },
	{ "mpr-16310.15", 0xc078a780, VIDEO_UNK, 0x000000, BYTE32 },
	{ "mpr-16311.16", 0x452a492b, VIDEO_UNK, 0x000001, BYTE32 },
	{ "mpr-16312.14", 0xa25fef5b, VIDEO_UNK, 0x000002, BYTE32 },
};

void place(std::vector<uint8_t> &r, const rom &ro, const std::vector<uint8_t> &d)
{
	const size_t off = ro.offset;
	switch (ro.k) {
	case WORD32:
		for (size_t i = 0; i + 1 < d.size() && off + 2 * i + 1 < r.size(); i += 2) { r[off + 2 * i] = d[i]; r[off + 2 * i + 1] = d[i + 1]; }
		break;
	case WORDSWAP:
		for (size_t i = 0; i + 1 < d.size() && off + i + 1 < r.size(); i += 2) { r[off + i] = d[i + 1]; r[off + i + 1] = d[i]; }
		break;
	case BYTE32:
		for (size_t i = 0; i < d.size() && off + 4 * i < r.size(); i++) r[off + 4 * i] = d[i];
		break;
	}
}

} // namespace

const char *region_name(int r) { return r >= 0 && r < N_REGIONS ? REGIONS[r].name : "?"; }

static result load_vf2(const std::string &path)
{
	result res;
	zip z;
	if (!z.open(path)) { res.error = "Couldn't read " + path + " as a zip file."; return res; }
	for (int r = 0; r < N_REGIONS; r++) res.regions[r].assign(REGIONS[r].size, REGIONS[r].fill);
	int bad = 0;
	std::vector<uint8_t> d;
	for (const rom &ro : ROMS) {
		rom_status st{ ro.name, ro.crc, ro.region, ro.offset, rom_status::OK };
		// by CRC first (any name), then report a same-named file with other contents
		const entry *e = nullptr;
		for (const entry &x : z.entries) if (x.crc == ro.crc && x.name == ro.name) { e = &x; break; }
		if (!e) for (const entry &x : z.entries) if (x.crc == ro.crc) { e = &x; break; }
		if (!e) {
			st.state = rom_status::MISSING;
			for (const entry &x : z.entries) if (x.name == ro.name) st.state = rom_status::WRONG;
		} else if (!z.read(*e, d)) {
			st.state = rom_status::DAMAGED;
		} else {
			place(res.regions[ro.region], ro, d);
		}
		if (st.state != rom_status::OK) {
			if (!bad++) res.error = "This isn't a complete Virtua Fighter 2 (Version 2.1) set, MAME's \"vf2\". Not usable:";
			res.error += std::string(" ") + ro.name;
		}
		res.roms.push_back(st);
	}
	res.ok = bad == 0;
	return res;
}

result load(const std::string &path)
{
	result res = load_vf2(path);
	if (res.ok && !vf1_models::prepare(path, res.vf1_models, res.error, &res.vf1_portraits)) res.ok = false;
	return res;
}

result import_file(const std::string &source, const std::string &destination)
{
	namespace fs = std::filesystem;
	const auto failure = [](const std::string &message) { result r; r.error = message; return r; };
	constexpr uintmax_t max_size = 256u << 20;
	std::error_code ec;
	const auto size = fs::file_size(source, ec);
	if (ec || size < 22 || size > max_size) return failure("Choose a ZIP file between 22 bytes and 256 MB.");
	const fs::path target(destination);
	const fs::path parent = target.has_parent_path() ? target.parent_path() : fs::path(".");
	fs::create_directories(parent, ec);
	if (ec) return failure("Couldn't create the ROM folder: " + ec.message());
	static std::atomic<unsigned> serial{0};
	fs::path staging;
	for (int attempt = 0; attempt < 10; ++attempt) {
		staging = parent / (".vf2-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
			+ "-" + std::to_string(serial.fetch_add(1)));
		if (fs::create_directory(staging, ec)) break;
		if (ec) return failure("Couldn't prepare the import: " + ec.message());
		staging.clear();
	}
	if (staging.empty()) return failure("Couldn't prepare a temporary ROM file.");
	struct cleanup {
		fs::path path;
		~cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
	} clean{staging};
	const auto temporary = staging / "vf2.zip";
	std::ifstream input(source, std::ios::binary);
	std::unique_ptr<FILE, decltype(&fclose)> output(fopen(temporary.string().c_str(), "wb"), fclose);
	if (!input || !output) return failure("Couldn't open the ROM file for copying.");
	char buffer[64 * 1024];
	uintmax_t copied = 0;
	while (input) {
		input.read(buffer, sizeof buffer);
		const auto count = input.gcount();
		copied += static_cast<uintmax_t>(count);
		if (copied > max_size) return failure("The ZIP exceeds 256 MB.");
		if (fwrite(buffer, 1, size_t(count), output.get()) != size_t(count))
			return failure("Couldn't copy the ROM file. Check free storage.");
	}
	if (!input.eof() || input.bad() || copied != size)
		return failure("The ROM file couldn't be copied completely. Try again after the file finishes downloading.");
	bool synced = fflush(output.get()) == 0;
#ifdef _WIN32
	if (synced) synced = _commit(_fileno(output.get())) == 0;
#else
	if (synced) synced = fsync(fileno(output.get())) == 0;
#endif
	const bool closed = fclose(output.release()) == 0;
	if (!synced || !closed) return failure("Couldn't save the ROM file to disk. Check free storage.");
	result loaded = load_vf2(temporary.string());
	if (!loaded.ok) return loaded;
	// Companion belongs to the installed location, never the provider/staging folder.
	if (!vf1_models::prepare(destination, loaded.vf1_models, loaded.error, &loaded.vf1_portraits)) { loaded.ok = false; return loaded; }
	// Same filesystem: readers see the old complete ZIP or the new complete ZIP.
	// On a platform where replacing an existing path is unsupported, fail safely.
	fs::rename(temporary, target, ec);
	if (ec) return failure("Couldn't save the ROM file: " + ec.message());
	return loaded;
}

} // namespace romset
