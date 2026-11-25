// SPDX-License-Identifier: Apache-2.0
// Minimal read-only ZIP reader (stored + deflate) with its own inflate implementation,
// so formats that are zip containers (3MF) need no extra dependency.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mi {

// Decompresses a raw DEFLATE stream (RFC 1951). `expectedSize` is the size recorded in the archive;
// the output must match it exactly. Returns false on corrupt data.
bool inflateRaw(const uint8_t* data, size_t size, size_t expectedSize, std::vector<uint8_t>& out);

class ZipArchive {
public:
    // The buffer must stay alive as long as the archive is used.
    bool open(const char* data, size_t size, std::string& error);

    bool contains(const std::string& name) const;
    // Extracts a file. Names are matched case-insensitively, ignoring a leading '/'.
    bool extract(const std::string& name, std::string& out, std::string& error) const;
    // Name (original spelling) of the first entry whose lower-cased name ends with `suffix`, or "".
    std::string findBySuffix(const std::string& suffix) const;

private:
    struct Entry {
        std::string name;
        uint16_t method = 0;
        uint32_t compressedSize = 0;
        uint32_t uncompressedSize = 0;
        uint32_t localHeaderOffset = 0;
    };
    static std::string normalize(const std::string& name);

    const char* data_ = nullptr;
    size_t size_ = 0;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
};

}  // namespace mi
