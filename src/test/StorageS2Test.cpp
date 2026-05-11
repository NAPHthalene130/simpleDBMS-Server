#include <iostream>
#include <filesystem>
#include <cstring>
#include "storage/object/StorageCommon.h"
#include "storage/manager/FileManager.h"

using namespace storage;

// Allocate a slot in pageData, writing tupleData at freeStart.
// Returns slotIndex (>=0) on success, -1 if page is full.
int allocateSlot(std::string& pageData, const std::string& tupleData) {
    DataPageHeader hdr;
    std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));

    std::uint32_t need = static_cast<std::uint32_t>(tupleData.size()) + kSlotSize;
    if (hdr.freeEnd - hdr.freeStart < need) return -1;

    std::memcpy(pageData.data() + hdr.freeStart, tupleData.data(), tupleData.size());

    PageSlot slot;
    slot.offset = hdr.freeStart;
    slot.flags = 0;
    std::uint32_t slotPos = hdr.freeEnd - kSlotSize;
    std::memcpy(pageData.data() + slotPos, &slot, sizeof(PageSlot));

    hdr.freeStart += static_cast<std::uint16_t>(tupleData.size());
    hdr.freeEnd   -= kSlotSize;
    int slotIndex = static_cast<int>(hdr.slotCount);
    hdr.slotCount++;
    std::memcpy(pageData.data(), &hdr, sizeof(DataPageHeader));
    return slotIndex;
}

bool test_allocateOneTuple() {
    const auto path = std::filesystem::path("data") / "test_s2_one.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1;
    hdr.freeStart = kDataPageHeader;
    hdr.freeEnd = kDataPageSize;
    hdr.slotCount = 0;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    std::string tuple = "v1|v2|v3";
    int slot = allocateSlot(page, tuple);
    if (slot != 0) { std::cerr << "FAIL: expected slot 0, got " << slot << std::endl; return false; }

    DataPageHeader after;
    std::memcpy(&after, page.data(), sizeof(DataPageHeader));
    if (after.slotCount != 1) { std::cerr << "FAIL: slotCount=" << after.slotCount << std::endl; return false; }
    if (after.freeStart != kDataPageHeader + tuple.size()) { std::cerr << "FAIL: freeStart=" << after.freeStart << std::endl; return false; }
    if (after.freeEnd != kDataPageSize - kSlotSize) { std::cerr << "FAIL: freeEnd=" << after.freeEnd << std::endl; return false; }

    // Verify tuple data at position
    std::string readTuple(page.data() + kDataPageHeader, tuple.size());
    if (readTuple != tuple) { std::cerr << "FAIL: tuple=" << readTuple << std::endl; return false; }

    // Verify slot entry (only one slot, at freeEnd + 0 since slotCount-1-0 = 0)
    PageSlot readSlot;
    std::memcpy(&readSlot, page.data() + after.freeEnd, sizeof(PageSlot));
    if (readSlot.offset != kDataPageHeader) { std::cerr << "FAIL: slot.offset=" << readSlot.offset << std::endl; return false; }
    if (readSlot.flags != 0) { std::cerr << "FAIL: slot.flags=" << readSlot.flags << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

bool test_allocateMultipleTuples() {
    const auto path = std::filesystem::path("data") / "test_s2_multi.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1;
    hdr.freeStart = kDataPageHeader;
    hdr.freeEnd = kDataPageSize;
    hdr.slotCount = 0;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    std::string t0 = "ROW|k1|x1|y1";
    std::string t1 = "ROW|k2|x2|y2";
    std::string t2 = "ROW|k3|x3";

    int s0 = allocateSlot(page, t0);
    int s1 = allocateSlot(page, t1);
    int s2 = allocateSlot(page, t2);

    if (s0 != 0 || s1 != 1 || s2 != 2) {
        std::cerr << "FAIL: slots " << s0 << "," << s1 << "," << s2 << std::endl;
        return false;
    }

    DataPageHeader after;
    std::memcpy(&after, page.data(), sizeof(DataPageHeader));
    if (after.slotCount != 3) { std::cerr << "FAIL: slotCount=" << after.slotCount << std::endl; return false; }
    std::uint32_t expectedSize = t0.size() + t1.size() + t2.size();
    if (after.freeStart != kDataPageHeader + expectedSize) {
        std::cerr << "FAIL: freeStart=" << after.freeStart << " expected=" << (kDataPageHeader + expectedSize) << std::endl;
        return false;
    }
    if (after.freeEnd != kDataPageSize - 3 * kSlotSize) {
        std::cerr << "FAIL: freeEnd=" << after.freeEnd << std::endl;
        return false;
    }

    // Verify all slots have correct offsets
    for (int i = 0; i < 3; ++i) {
        std::uint32_t pos = after.freeEnd + (after.slotCount - 1 - i) * kSlotSize;
        PageSlot slot;
        std::memcpy(&slot, page.data() + pos, sizeof(PageSlot));
        if (slot.flags != 0) { std::cerr << "FAIL: slot[" << i << "].flags=" << slot.flags << std::endl; return false; }
    }

    std::filesystem::remove(path);
    return true;
}

bool test_pageFull() {
    const auto path = std::filesystem::path("data") / "test_s2_full.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1;
    hdr.freeStart = kDataPageHeader;
    hdr.freeEnd = kDataPageSize;
    hdr.slotCount = 0;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    // Fill page with large tuples
    std::string big(3900, 'x'); // 3900 bytes of data + 4 byte slot = 3904 needed
    int s = allocateSlot(page, big);
    if (s != 0) { std::cerr << "FAIL: first alloc should succeed, got " << s << std::endl; return false; }

    // Now page has ~64 bytes free (4096 - 32 - 3900 - 4 = 160... wait
    // freeStart = 32 + 3900 = 3932, freeEnd = 4096 - 4 = 4092, free space = 4092 - 3932 = 160
    // But we need another big tuple
    int s2 = allocateSlot(page, big);
    if (s2 != -1) { std::cerr << "FAIL: second alloc should fail (page full), got " << s2 << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

bool test_persistAndReload() {
    const auto path = std::filesystem::path("data") / "test_s2_persist.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1;
    hdr.freeStart = kDataPageHeader;
    hdr.freeEnd = kDataPageSize;
    hdr.slotCount = 0;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    allocateSlot(page, "t1|a|b");
    allocateSlot(page, "t2|c|d");
    allocateSlot(page, "t3|e|f");

    if (!FileManager::writePage(path, 1, page)) {
        std::cerr << "FAIL: writePage" << std::endl; return false;
    }

    std::string loaded = FileManager::readPage(path, 1);
    DataPageHeader loadedHdr;
    std::memcpy(&loadedHdr, loaded.data(), sizeof(DataPageHeader));

    if (loadedHdr.slotCount != 3) { std::cerr << "FAIL persist: slotCount=" << loadedHdr.slotCount << std::endl; return false; }
    if (loadedHdr.pageId != 1) { std::cerr << "FAIL persist: pageId=" << loadedHdr.pageId << std::endl; return false; }

    // Verify slot 0 (first allocated, at highest slot address)
    std::uint32_t s0Pos = loadedHdr.freeEnd + (loadedHdr.slotCount - 1 - 0) * kSlotSize;
    PageSlot s0;
    std::memcpy(&s0, loaded.data() + s0Pos, sizeof(PageSlot));
    std::string t0v = "t1|a|b";
    std::string t0(loaded.data() + s0.offset, t0v.size());
    if (t0 != t0v) { std::cerr << "FAIL persist: tuple0=" << t0 << " offset=" << s0.offset << std::endl; return false; }

    // Verify slot 2 (last allocated, at freeEnd)
    std::uint32_t s2Pos = loadedHdr.freeEnd + (loadedHdr.slotCount - 1 - 2) * kSlotSize;
    PageSlot s2;
    std::memcpy(&s2, loaded.data() + s2Pos, sizeof(PageSlot));
    std::string t2v = "t3|e|f";
    std::string t2(loaded.data() + s2.offset, t2v.size());
    if (t2 != t2v) { std::cerr << "FAIL persist: tuple2=" << t2 << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

int main() {
    std::filesystem::create_directories("data");
    bool ok = true;
    ok = test_allocateOneTuple() && ok;
    ok = test_allocateMultipleTuples() && ok;
    ok = test_pageFull() && ok;
    ok = test_persistAndReload() && ok;
    if (ok) std::cout << "S2 PASSED" << std::endl;
    else    std::cout << "S2 FAILED" << std::endl;
    return ok ? 0 : 1;
}
