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
    slot.offset = hdr.freeStart;
    slot.flags = 0;
    std::uint32_t slotPos = hdr.freeEnd - kSlotSize;
    std::memcpy(pageData.data() + slotPos, &slot, sizeof(PageSlot));
    hdr.freeStart += static_cast<std::uint16_t>(data.size());
    hdr.freeEnd   -= kSlotSize;
    int slotIndex = static_cast<int>(hdr.slotCount);
    hdr.slotCount++;
    std::memcpy(pageData.data(), &hdr, sizeof(DataPageHeader));
    return slotIndex;
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
    std::string tuple(start, static_cast<std::size_t>(end - start));
    out = deserializeRow(tuple);
    return true;
}

bool test_write3read3() {
    const auto path = std::filesystem::path("data") / "test_s3_rw.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    int s0 = allocateSlot(page, "k1|x1|y1");
    int s1 = allocateSlot(page, "k2|x2|y2");
    int s2 = allocateSlot(page, "k3|x3|y3");

    if (!FileManager::writePage(path, 1, page)) { std::cerr << "FAIL write" << std::endl; return false; }
    std::string loaded = FileManager::readPage(path, 1);

    Row r0, r1, r2;
    if (!readTuple(loaded, s0, r0)) { std::cerr << "FAIL read slot0" << std::endl; return false; }
    if (!readTuple(loaded, s1, r1)) { std::cerr << "FAIL read slot1" << std::endl; return false; }
    if (!readTuple(loaded, s2, r2)) { std::cerr << "FAIL read slot2" << std::endl; return false; }

    if (r0.values.size() != 3 || r0.values[0] != "k1" || r0.values[1] != "x1" || r0.values[2] != "y1")
        { std::cerr << "FAIL r0: " << join(r0.values,",") << std::endl; return false; }
    if (r1.values.size() != 3 || r1.values[0] != "k2")
        { std::cerr << "FAIL r1: " << join(r1.values,",") << std::endl; return false; }
    if (r2.values.size() != 3 || r2.values[0] != "k3")
        { std::cerr << "FAIL r2: " << join(r2.values,",") << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

bool test_variableSizeTuples() {
    const auto path = std::filesystem::path("data") / "test_s3_var.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    int s0 = allocateSlot(page, "short");
    int s1 = allocateSlot(page, "medium_length_value");
    int s2 = allocateSlot(page, "x");

    Row r0, r1, r2;
    if (!readTuple(page, s0, r0) || r0.values[0] != "short")     { std::cerr << "FAIL var0" << std::endl; return false; }
    if (!readTuple(page, s1, r1) || r1.values[0] != "medium_length_value") { std::cerr << "FAIL var1" << std::endl; return false; }
    if (!readTuple(page, s2, r2) || r2.values[0] != "x")          { std::cerr << "FAIL var2" << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

bool test_multiColumnRead() {
    const auto path = std::filesystem::path("data") / "test_s3_mcol.trd";
    std::filesystem::remove(path);

    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    int s = allocateSlot(page, "pk001|Alice|25|engineer");
    Row r;
    if (!readTuple(page, s, r) || r.values.size() != 4) { std::cerr << "FAIL mcol size" << std::endl; return false; }
    if (r.values[0] != "pk001" || r.values[1] != "Alice" || r.values[2] != "25" || r.values[3] != "engineer")
        { std::cerr << "FAIL mcol vals" << std::endl; return false; }

    std::filesystem::remove(path);
    return true;
}

bool test_invalidSlot() {
    std::string page(4096, '\0');
    DataPageHeader hdr;
    hdr.pageId = 1; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(page.data(), &hdr, sizeof(DataPageHeader));

    allocateSlot(page, "only");
    Row r;
    if (readTuple(page, -1, r)) { std::cerr << "FAIL expected false for slot=-1" << std::endl; return false; }
    if (readTuple(page, 5, r))  { std::cerr << "FAIL expected false for slot=5" << std::endl; return false; }
    return true;
}

int main() {
    std::filesystem::create_directories("data");
    bool ok = true;
    ok = test_write3read3() && ok;
    ok = test_variableSizeTuples() && ok;
    ok = test_multiColumnRead() && ok;
    ok = test_invalidSlot() && ok;
    if (ok) std::cout << "S3 PASSED" << std::endl;
    else    std::cout << "S3 FAILED" << std::endl;
    return ok ? 0 : 1;
}
