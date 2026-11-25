// SPDX-License-Identifier: Apache-2.0
#include "zip.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace mi {

// ============================================================================ inflate
// Canonical-Huffman decoder in the style of zlib's "puff" reference implementation.

namespace {

constexpr int kMaxBits = 15;

struct BitReader {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t buf = 0;
    int count = 0;
    bool failed = false;

    int bits(int need) {
        uint32_t val = buf;
        while (count < need) {
            if (p >= end) {
                failed = true;
                return 0;
            }
            val |= static_cast<uint32_t>(*p++) << count;
            count += 8;
        }
        buf = val >> need;
        count -= need;
        return static_cast<int>(val & ((1u << need) - 1u));
    }
};

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[288];
};

// Returns 0 for a complete code, >0 if incomplete, <0 if over-subscribed.
int construct(Huffman& h, const short* length, int n) {
    for (int len = 0; len <= kMaxBits; ++len) h.count[len] = 0;
    for (int s = 0; s < n; ++s) ++h.count[length[s]];
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);
    for (int s = 0; s < n; ++s)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = static_cast<short>(s);
    return left;
}

int decode(BitReader& s, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= s.bits(1);
        if (s.failed) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const short kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateCodes(BitReader& s, std::vector<uint8_t>& out, size_t limit, const Huffman& lencode, const Huffman& distcode) {
    while (true) {
        int sym = decode(s, lencode);
        if (sym < 0) return false;
        if (sym < 256) {
            if (out.size() >= limit) return false;
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            const int len = kLenBase[sym] + s.bits(kLenExtra[sym]);
            const int dsym = decode(s, distcode);
            if (dsym < 0 || dsym >= 30) return false;
            const size_t dist = static_cast<size_t>(kDistBase[dsym] + s.bits(kDistExtra[dsym]));
            if (s.failed || dist > out.size() || out.size() + static_cast<size_t>(len) > limit) return false;
            size_t from = out.size() - dist;
            for (int i = 0; i < len; ++i) out.push_back(out[from + static_cast<size_t>(i)]);  // may overlap, so byte by byte
        }
    }
}

}  // namespace

bool inflateRaw(const uint8_t* data, size_t size, size_t expectedSize, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(expectedSize);
    BitReader s{data, data + size};

    static Huffman fixedLen, fixedDist;
    static bool fixedReady = false;
    if (!fixedReady) {
        short lengths[288];
        int i = 0;
        for (; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < 288; ++i) lengths[i] = 8;
        construct(fixedLen, lengths, 288);
        for (i = 0; i < 30; ++i) lengths[i] = 5;
        construct(fixedDist, lengths, 30);
        fixedReady = true;
    }

    int last;
    do {
        last = s.bits(1);
        const int type = s.bits(2);
        if (s.failed) return false;
        if (type == 0) {  // stored block
            s.buf = 0;
            s.count = 0;
            if (s.end - s.p < 4) return false;
            const unsigned len = s.p[0] | (static_cast<unsigned>(s.p[1]) << 8);
            const unsigned nlen = s.p[2] | (static_cast<unsigned>(s.p[3]) << 8);
            s.p += 4;
            if (len != (~nlen & 0xFFFFu)) return false;
            if (static_cast<size_t>(s.end - s.p) < len || out.size() + len > expectedSize) return false;
            out.insert(out.end(), s.p, s.p + len);
            s.p += len;
        } else if (type == 1) {
            if (!inflateCodes(s, out, expectedSize, fixedLen, fixedDist)) return false;
        } else if (type == 2) {
            const int nlen = s.bits(5) + 257;
            const int ndist = s.bits(5) + 1;
            const int ncode = s.bits(4) + 4;
            if (s.failed || nlen > 286 || ndist > 30) return false;
            static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            short lengths[320];
            int idx = 0;
            for (; idx < ncode; ++idx) lengths[order[idx]] = static_cast<short>(s.bits(3));
            for (; idx < 19; ++idx) lengths[order[idx]] = 0;
            Huffman lencode, distcode;
            if (construct(lencode, lengths, 19) != 0) return false;
            idx = 0;
            while (idx < nlen + ndist) {
                int sym = decode(s, lencode);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[idx++] = static_cast<short>(sym);
                } else {
                    int rep, val = 0;
                    if (sym == 16) {
                        if (idx == 0) return false;
                        val = lengths[idx - 1];
                        rep = 3 + s.bits(2);
                    } else if (sym == 17) {
                        rep = 3 + s.bits(3);
                    } else {
                        rep = 11 + s.bits(7);
                    }
                    if (s.failed || idx + rep > nlen + ndist) return false;
                    while (rep--) lengths[idx++] = static_cast<short>(val);
                }
            }
            if (lengths[256] == 0) return false;  // no end-of-block code
            int err = construct(lencode, lengths, nlen);
            if (err != 0 && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return false;
            err = construct(distcode, lengths + nlen, ndist);
            if (err != 0 && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return false;
            if (!inflateCodes(s, out, expectedSize, lencode, distcode)) return false;
        } else {
            return false;
        }
    } while (!last);
    return out.size() == expectedSize;
}

// ============================================================================ zip

namespace {

uint16_t rd16(const char* p) {
    return static_cast<uint16_t>(static_cast<uint8_t>(p[0]) | (static_cast<uint8_t>(p[1]) << 8));
}
uint32_t rd32(const char* p) {
    return static_cast<uint32_t>(static_cast<uint8_t>(p[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(p[3])) << 24);
}

}  // namespace

std::string ZipArchive::normalize(const std::string& name) {
    std::string n;
    for (char c : name) n += c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!n.empty() && n[0] == '/') n.erase(0, 1);
    return n;
}

bool ZipArchive::open(const char* data, size_t size, std::string& error) {
    data_ = data;
    size_ = size;
    entries_.clear();
    index_.clear();
    if (size < 22) {
        error = "Not a ZIP archive (file too small).";
        return false;
    }
    // End of central directory record: search backwards (it may be followed by a comment).
    size_t eocd = std::string::npos;
    const size_t lowest = size > 22 + 65535 ? size - 22 - 65535 : 0;
    for (size_t i = size - 22 + 1; i-- > lowest;) {
        if (rd32(data + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "Not a ZIP archive (no end-of-directory record).";
        return false;
    }
    const uint32_t entryCount = rd16(data + eocd + 10);
    const uint32_t dirSize = rd32(data + eocd + 12);
    const uint32_t dirOffset = rd32(data + eocd + 16);
    if (entryCount == 0xFFFFu || dirSize == 0xFFFFFFFFu || dirOffset == 0xFFFFFFFFu) {
        error = "ZIP64 archives (over 4 GB) are not supported.";
        return false;
    }
    if (static_cast<uint64_t>(dirOffset) + dirSize > size) {
        error = "Corrupt ZIP archive (bad directory offset).";
        return false;
    }
    size_t p = dirOffset;
    const size_t dirEnd = static_cast<size_t>(dirOffset) + dirSize;
    for (uint32_t i = 0; i < entryCount; ++i) {
        if (p + 46 > dirEnd || rd32(data + p) != 0x02014b50u) {
            error = "Corrupt ZIP archive (bad directory entry).";
            return false;
        }
        Entry e;
        const uint16_t flags = rd16(data + p + 8);
        e.method = rd16(data + p + 10);
        e.compressedSize = rd32(data + p + 20);
        e.uncompressedSize = rd32(data + p + 24);
        const uint16_t nameLen = rd16(data + p + 28);
        const uint16_t extraLen = rd16(data + p + 30);
        const uint16_t commentLen = rd16(data + p + 32);
        e.localHeaderOffset = rd32(data + p + 42);
        if (p + 46 + nameLen > dirEnd) {
            error = "Corrupt ZIP archive (bad file name).";
            return false;
        }
        e.name.assign(data + p + 46, nameLen);
        if (flags & 1u) e.method = 0xFFFF;  // encrypted: reported when extracted
        index_[normalize(e.name)] = entries_.size();
        entries_.push_back(std::move(e));
        p += 46u + nameLen + extraLen + commentLen;
    }
    return true;
}

bool ZipArchive::contains(const std::string& name) const { return index_.count(normalize(name)) != 0; }

std::string ZipArchive::findBySuffix(const std::string& suffix) const {
    const std::string want = normalize(suffix);
    for (const Entry& e : entries_) {
        const std::string n = normalize(e.name);
        if (n.size() >= want.size() && n.compare(n.size() - want.size(), want.size(), want) == 0) return e.name;
    }
    return {};
}

bool ZipArchive::extract(const std::string& name, std::string& out, std::string& error) const {
    const auto it = index_.find(normalize(name));
    if (it == index_.end()) {
        error = "File '" + name + "' not found inside the archive.";
        return false;
    }
    const Entry& e = entries_[it->second];
    if (e.method == 0xFFFF) {
        error = "'" + name + "' is encrypted, which is not supported.";
        return false;
    }
    const size_t lh = e.localHeaderOffset;
    if (lh + 30 > size_ || rd32(data_ + lh) != 0x04034b50u) {
        error = "Corrupt ZIP archive (bad local header for '" + name + "').";
        return false;
    }
    const size_t start = lh + 30u + rd16(data_ + lh + 26) + rd16(data_ + lh + 28);
    if (start > size_ || e.compressedSize > size_ - start) {
        error = "Corrupt ZIP archive ('" + name + "' is truncated).";
        return false;
    }
    const auto* src = reinterpret_cast<const uint8_t*>(data_ + start);
    if (e.method == 0) {
        out.assign(reinterpret_cast<const char*>(src), e.compressedSize);
        return true;
    }
    if (e.method == 8) {
        std::vector<uint8_t> buf;
        if (!inflateRaw(src, e.compressedSize, e.uncompressedSize, buf)) {
            error = "Corrupt compressed data for '" + name + "'.";
            return false;
        }
        out.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
        return true;
    }
    error = "'" + name + "' uses an unsupported compression method (" + std::to_string(e.method) + ").";
    return false;
}

}  // namespace mi
