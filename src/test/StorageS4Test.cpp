#include <iostream>
#include <filesystem>
#include <cstring>
#include "storage/object/StorageCommon.h"
#include "storage/manager/FileManager.h"

using namespace storage;

int allocateSlot(std::string& pageData, const std::string& tupleData) {
    DataPageHeader hdr;
    std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
    std::string data = tupleData + '\n';
    std::uint32_t need = static_cast<std::uint32_t>(data.size()) + kSlotSize;
    if (hdr.freeEnd - hdr.freeStart < need) return -1;
    std::memcpy(pageData.data() + hdr.freeStart, data.data(), data.size());
    PageSlot slot;
    slot.offset = hdr.freeStart; slot.flags = 0;
    std::uint32_t slotPos = hdr.freeEnd - kSlotSize;
    std::memcpy(pageData.data() + slotPos, &slot, sizeof(PageSlot));
    hdr.freeStart += static_cast<std::uint16_t>(data.size());
    hdr.freeEnd   -= kSlotSize;
    int idx = static_cast<int>(hdr.slotCount);
    hdr.slotCount++;
    std::memcpy(pageData.data(), &hdr, sizeof(DataPageHeader));
    return idx;
}

bool readTuple(const std::string& pageData, int slotIndex, Row& out) {
    DataPageHeader hdr;
    std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
    if (slotIndex < 0 || static_cast<std::uint32_t>(slotIndex) >= hdr.slotCount) return false;
    std::uint32_t slotPos = hdr.freeEnd + (hdr.slotCount - 1 - slotIndex) * kSlotSize;
    PageSlot slot;
    std::memcpy(&slot, pageData.data() + slotPos, sizeof(PageSlot));
    if (slot.flags != 0) return false;
    const char* start = pageData.data() + slot.offset;
    const char* end = static_cast<const char*>(std::memchr(start, '\n', pageData.size() - slot.offset));
    if (!end) return false;
    out = deserializeRow(std::string(start, static_cast<std::size_t>(end - start)));
    return true;
}

bool markSlotDeleted(std::string& pageData, int slotIndex) {
    DataPageHeader hdr;
    std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
    if (slotIndex < 0 || static_cast<std::uint32_t>(slotIndex) >= hdr.slotCount) return false;
    std::uint32_t slotPos = hdr.freeEnd + (hdr.slotCount - 1 - slotIndex) * kSlotSize;
    PageSlot slot;
    std::memcpy(&slot, pageData.data() + slotPos, sizeof(PageSlot));
    if (slot.flags != 0) return false;
    slot.flags = 1;
    std::memcpy(pageData.data() + slotPos, &slot, sizeof(PageSlot));
    return true;
}

bool test_deleteMiddle() {
    const auto path = std::filesystem::path("data") / "test_s4_mid.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    int s0 = allocateSlot(page, "row_A");
    int s1 = allocateSlot(page, "row_B");
    int s2 = allocateSlot(page, "row_C");

    // Delete middle slot
    if (!markSlotDeleted(page, s1)) { std::cerr << "FAIL markDeleted" << std::endl; return false; }

    FileManager::writePage(path, 1, page);
    std::string loaded = FileManager::readPage(path, 1);

    Row r0, r1, r2;
    bool ok0 = readTuple(loaded, s0, r0);
    bool ok1 = readTuple(loaded, s1, r1);
    bool ok2 = readTuple(loaded, s2, r2);

    if (!ok0 || r0.values[0] != "row_A") { std::cerr << "FAIL: slot0 should be active" << std::endl; return false; }
    if (ok1)                              { std::cerr << "FAIL: slot1 should be deleted" << std::endl; return false; }
    if (!ok2 || r2.values[0] != "row_C") { std::cerr << "FAIL: slot2 should be active" << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

bool test_deleteFirstAndLast() {
    const auto path = std::filesystem::path("data") / "test_s4_ends.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    int s0 = allocateSlot(page, "first");
    int s1 = allocateSlot(page, "middle");
    int s2 = allocateSlot(page, "last");

    markSlotDeleted(page, s0);
    markSlotDeleted(page, s2);

    Row r;
    if (readTuple(page, s0, r)) { std::cerr << "FAIL: s0 deleted" << std::endl; return false; }
    if (!readTuple(page, s1, r) || r.values[0] != "middle") { std::cerr << "FAIL: s1 active" << std::endl; return false; }
    if (readTuple(page, s2, r)) { std::cerr << "FAIL: s2 deleted" << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

bool test_doubleDelete() {
    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    int s = allocateSlot(page, "doomed");
    if (!markSlotDeleted(page, s))   { std::cerr << "FAIL first delete" << std::endl; return false; }
    if (markSlotDeleted(page, s))    { std::cerr << "FAIL: second delete should fail (already deleted)" << std::endl; return false; }
    return true;
}

bool test_deletePersist() {
    const auto path = std::filesystem::path("data") / "test_s4_persist.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr; hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    allocateSlot(page, "keep");
    allocateSlot(page, "toss");
    allocateSlot(page, "keep2");
    markSlotDeleted(page, 1);  // delete "toss"

    FileManager::writePage(path, 1, page);

    std::string loaded = FileManager::readPage(path, 1);
    Row r;
    if (!readTuple(loaded, 0, r) || r.values[0] != "keep")  { std::cerr << "FAIL persist s0" << std::endl; return false; }
    if (readTuple(loaded, 1, r))                              { std::cerr << "FAIL persist s1 deleted" << std::endl; return false; }
    if (!readTuple(loaded, 2, r) || r.values[0] != "keep2") { std::cerr << "FAIL persist s2" << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

int main() {
    std::filesystem::create_directories("data");
    bool ok = true;
    ok = test_deleteMiddle() && ok;
    ok = test_deleteFirstAndLast() && ok;
    ok = test_doubleDelete() && ok;
    ok = test_deletePersist() && ok;
    if (ok) std::cout << "S4 PASSED" << std::endl;
    else    std::cout << "S4 FAILED" << std::endl;
    return ok ? 0 : 1;
}
