#include "vgm_patch_extractor.hpp"

#include <zlib.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<std::uint8_t> readFile(const std::string& path) {
    // zlib's gz* API also reads uncompressed files ("transparent" mode),
    // so this handles both .vgm and gzip-compressed .vgz.
    gzFile file = gzopen(path.c_str(), "rb");
    if (!file) throw std::runtime_error("Tidak bisa membuka file: " + path);

    std::vector<std::uint8_t> data;
    std::uint8_t buffer[16 * 1024];
    int count = 0;
    while ((count = gzread(file, buffer, sizeof(buffer))) > 0)
        data.insert(data.end(), buffer, buffer + count);

    int err = Z_OK;
    const char* message = gzerror(file, &err);
    gzclose(file);
    if (count < 0 || (err != Z_OK && err != Z_STREAM_END))
        throw std::runtime_error(std::string("Gagal membaca/decompress file: ") +
                                 (message ? message : "unknown zlib error"));
    return data;
}

static std::uint32_t u32(const std::vector<std::uint8_t>& d, std::size_t p) {
    if (p + 4 > d.size()) throw std::runtime_error("Header VGM terpotong");
    return static_cast<std::uint32_t>(d[p]) |
           (static_cast<std::uint32_t>(d[p + 1]) << 8) |
           (static_cast<std::uint32_t>(d[p + 2]) << 16) |
           (static_cast<std::uint32_t>(d[p + 3]) << 24);
}

static void need(const std::vector<std::uint8_t>& d, std::size_t p, std::size_t n) {
    if (p + n > d.size()) throw std::runtime_error("Command VGM terpotong");
}

static std::size_t dataStart(const std::vector<std::uint8_t>& d) {
    if (d.size() < 0x40 || d[0] != 'V' || d[1] != 'g' ||
        d[2] != 'm' || d[3] != ' ') {
        throw std::runtime_error("File bukan VGM yang valid");
    }

    const std::uint32_t version = u32(d, 0x08);
    // VGM 1.50+: data offset is relative to 0x34; zero means legacy 0x40.
    if (version >= 0x150) {
        const std::uint32_t offset = u32(d, 0x34);
        if (offset != 0) {
            const std::size_t start = 0x34u + offset;
            if (start >= d.size()) throw std::runtime_error("Data offset di luar file");
            return start;
        }
    }
    return 0x40;
}

static void parseVgm(const std::vector<std::uint8_t>& d,
                     VgmPatchExtractor& extractor) {
    std::size_t p = dataStart(d);
    std::uint64_t sample = 0;

    while (p < d.size()) {
        const std::uint8_t cmd = d[p++];

        if (cmd == 0x66) break; // End of sound data

        if (cmd == 0x52 || cmd == 0x53 || cmd == 0x54 || cmd == 0x55) {
            need(d, p, 2);
            const std::uint8_t address = d[p++];
            const std::uint8_t value = d[p++];
            switch (cmd) {
                case 0x52:
                    extractor.onWrite(VgmChip::YM2612, 0, address, value, sample);
                    break;
                case 0x53:
                    extractor.onWrite(VgmChip::YM2612, 1, address, value, sample);
                    break;
                case 0x54:
                    extractor.onWrite(VgmChip::YM2151, 0, address, value, sample);
                    break;
                case 0x55:
                    extractor.onWrite(VgmChip::YM2203, 0, address, value, sample);
                    break;
            }
            continue;
        }

        if (cmd == 0x61) {
            need(d, p, 2);
            sample += static_cast<std::uint64_t>(d[p]) |
                      (static_cast<std::uint64_t>(d[p + 1]) << 8);
            p += 2;
        } else if (cmd == 0x62) {
            sample += 735;
        } else if (cmd == 0x63) {
            sample += 882;
        } else if (cmd >= 0x70 && cmd <= 0x7F) {
            sample += (cmd & 0x0F) + 1;
        } else if (cmd == 0x64) {
            need(d, p, 3);
            p += 3; // Override wait command
        } else if (cmd == 0x67) {
            need(d, p, 6);
            if (d[p] != 0x66) throw std::runtime_error("VGM data block marker tidak valid");
            const std::uint32_t size = u32(d, p + 2);
            p += 6;
            need(d, p, size);
            p += size;
        } else if (cmd == 0x68) {
            need(d, p, 11);
            p += 11;
        } else {
            // Skip other documented command families so chip writes after them
            // stay aligned. This extractor intentionally only captures the
            // three requested FM chips; all other chip writes are ignored.
            std::size_t args = 0;
            if (cmd >= 0x30 && cmd <= 0x3F) args = 1;
            else if (cmd == 0x4F || cmd == 0x50) args = 1;
            else if (cmd >= 0x51 && cmd <= 0x5F) args = 2;
            else if (cmd >= 0xA0 && cmd <= 0xBF) args = 2;
            else if (cmd >= 0xC0 && cmd <= 0xDF) args = 3;
            else if (cmd >= 0xE0) args = 4;
            else if (cmd >= 0x80 && cmd <= 0x8F) {
                // DAC write + wait (wait is encoded in the low nibble).
                sample += (cmd & 0x0F);
                continue;
            } else if (cmd >= 0x90 && cmd <= 0x95) {
                static const std::size_t lengths[] = {4, 4, 5, 10, 1, 4};
                args = lengths[cmd - 0x90];
            } else {
                throw std::runtime_error("Opcode VGM tidak dikenal: " +
                                         std::to_string(cmd) + " pada offset " +
                                         std::to_string(p - 1));
            }
            need(d, p, args);
            p += args;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " input.vgm[.vgz] [output.json]\n";
        return 2;
    }

    try {
        const auto data = readFile(argv[1]);
        VgmPatchExtractor extractor;
        parseVgm(data, extractor);

        const std::string json = extractor.toJson();
        if (argc == 3) {
            std::ofstream out(argv[2], std::ios::binary);
            if (!out) throw std::runtime_error("Tidak bisa menulis output JSON");
            out << json;
            std::cout << "Patch JSON disimpan ke: " << argv[2] << '\n';
        } else {
            std::cout << json;
        }

        std::cerr << "Selesai. Snapshot patch ditemukan: "
                  << extractor.patches().size() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
