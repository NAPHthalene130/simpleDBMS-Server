#include <iostream>
#include <filesystem>
#include <cstring>
#include <functional>
#include "storage/object/StorageCommon.h"
#include "storage/manager/FileManager.h"

using namespace storage;

int allocateSlot(std::string& pageData, const std::string& tupleData) {
    DataPageHeader hdr; std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
    std::string data = tupleData + '\n';
    if (hdr.freeEnd - hdr.freeStart < data.size() + kSlotSize) return -1;
    std::memcpy(pageData.data() + hdr.freeStart, data.data(), data.size());
    PageSlot slot; slot.offset = hdr.freeStart; slot.flags = 0;
    std::memcpy(pageData.data() + hdr.freeEnd - kSlotSize, &slot, sizeof(PageSlot));
    hdr.freeStart += static_cast<std::uint16_t>(data.size()); hdr.freeEnd -= kSlotSize;
    int idx = static_cast<int>(hdr.slotCount); hdr.slotCount++;
    std::memcpy(pageData.data(), &hdr, sizeof(DataPageHeader)); return idx;
}

bool markSlotDeleted(std::string& pageData, int slotIndex) {
    DataPageHeader hdr; std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
    if (slotIndex < 0 || static_cast<std::uint32_t>(slotIndex) >= hdr.slotCount) return false;
    std::uint32_t pos = hdr.freeEnd + (hdr.slotCount - 1 - slotIndex) * kSlotSize;
    PageSlot slot; std::memcpy(&slot, pageData.data() + pos, sizeof(PageSlot));
    if (slot.flags != 0) return false;
    slot.flags = 1; std::memcpy(pageData.data() + pos, &slot, sizeof(PageSlot)); return true;
}

void scanPage(const std::string& pageData, std::function<void(int slotIndex, const Row&)> visitor) {
    DataPageHeader hdr; std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
    for (std::uint32_t i = 0; i < hdr.slotCount; ++i) {
        std::uint32_t pos = hdr.freeEnd + (hdr.slotCount - 1 - i) * kSlotSize;
        PageSlot slot; std::memcpy(&slot, pageData.data() + pos, sizeof(PageSlot));
        if (slot.flags != 0) continue;
        const char* start = pageData.data() + slot.offset;
        const char* end = static_cast<const char*>(std::memchr(start, '\n', pageData.size() - slot.offset));
        if (!end) continue;
        Row row = deserializeRow(std::string(start, static_cast<std::size_t>(end - start)));
        visitor(static_cast<int>(i), row);
    }
}

bool test_scanAllActive() {
    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    allocateSlot(page, "a1|b1");
    allocateSlot(page, "a2|b2");
    allocateSlot(page, "a3|b3");

    int count = 0;
    std::string vals;
    scanPage(page, [&](int, const Row& r) { ++count; vals += r.values[0] + ","; });

    if (count != 3)     { std::cerr << "FAIL count=" << count << std::endl; return false; }
    if (vals != "a1,a2,a3,") { std::cerr << "FAIL vals=" << vals << std::endl; return false; }
    return true;
}

bool test_scanSkipsDeleted() {
    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    allocateSlot(page, "keep1");
    allocateSlot(page, "del1");
    allocateSlot(page, "keep2");
    allocateSlot(page, "del2");
    allocateSlot(page, "keep3");
    markSlotDeleted(page, 1);
    markSlotDeleted(page, 3);

    int count = 0;
    std::string vals;
    scanPage(page, [&](int, const Row& r) { ++count; vals += r.values[0] + ","; });

    if (count != 3)                          { std::cerr << "FAIL count=" << count << std::endl; return false; }
    if (vals != "keep1,keep2,keep3,")        { std::cerr << "FAIL vals=" << vals << std::endl; return false; }
    return true;
}

bool test_scanAllDeleted() {
    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    allocateSlot(page, "d1"); allocateSlot(page, "d2"); allocateSlot(page, "d3");
    markSlotDeleted(page, 0); markSlotDeleted(page, 1); markSlotDeleted(page, 2);

    int count = 0;
    scanPage(page, [&](int, const Row&) { ++count; });
    if (count != 0) { std::cerr << "FAIL count=" << count << std::endl; return false; }
    return true;
}

bool test_scanSlotIndices() {
    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    allocateSlot(page, "s0");
    allocateSlot(page, "s1");
    allocateSlot(page, "s2");
    markSlotDeleted(page, 1); // delete middle

    std::string indices;
    scanPage(page, [&](int idx, const Row&) { indices += std::to_string(idx) + ","; });

    if (indices != "0,2,") { std::cerr << "FAIL indices=" << indices << std::endl; return false; }
    return true;
}

bool test_scanEmptyPage() {
    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    int count = 0;
    scanPage(page, [&](int, const Row&) { ++count; });
    if (count != 0) { std::cerr << "FAIL empty count=" << count << std::endl; return false; }
    return true;
}

int main() {
    std::filesystem::create_directories("data");
    bool ok = true;
    ok = test_scanAllActive() && ok;
    ok = test_scanSkipsDeleted() && ok;
    ok = test_scanAllDeleted() && ok;
    ok = test_scanSlotIndices() && ok;
    ok = test_scanEmptyPage() && ok;
    if (ok) std::cout << "S5 PASSED" << std::endl;
    else    std::cout << "S5 FAILED" << std::endl;
    return ok ? 0 : 1;
}
