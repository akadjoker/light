#include <stdio.h>
#include <string.h>

#include "lbc.h"

using namespace light;

static int failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_crc32_known_vectors()
{
    CHECK(crc32(nullptr, 0) == 0x00000000u);
    const u8 check[] = "123456789";
    CHECK(crc32(check, 9) == 0xCBF43926u);
}

static void build_file(u8* out, const u8* payload, u32 payload_size, u32 flags = 0)
{
    lbc_write_header(out, flags, payload, payload_size);
    if (payload_size)
        memcpy(out + LBC_HEADER_SIZE, payload, payload_size);
}

static void test_roundtrip()
{
    const u8 payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    u8 file[LBC_HEADER_SIZE + sizeof(payload)];
    build_file(file, payload, sizeof(payload), 0xABCD);

    LbcHeader h;
    Status s = lbc_read_header(file, sizeof(file), &h);
    CHECK(s.ok());
    CHECK(h.major == LBC_MAJOR && h.minor == LBC_MINOR);
    CHECK(h.flags == 0xABCD);
    CHECK(h.payload_size == sizeof(payload));
    CHECK(h.checksum == crc32(payload, sizeof(payload)));
}

static void test_empty_payload()
{
    u8 file[LBC_HEADER_SIZE];
    build_file(file, nullptr, 0);
    CHECK(lbc_read_header(file, sizeof(file), nullptr).ok());
}

static void test_rejects_bad_magic()
{
    const u8 payload[] = {1, 2, 3};
    u8 file[LBC_HEADER_SIZE + 3];
    build_file(file, payload, 3);
    file[0] = 'X';
    CHECK(lbc_read_header(file, sizeof(file), nullptr).code == ErrorCode::BadMagic);
}

static void test_rejects_future_version()
{
    const u8 payload[] = {1, 2, 3};
    u8 file[LBC_HEADER_SIZE + 3];
    build_file(file, payload, 3);
    file[4] = (u8)(LBC_MAJOR + 1);
    CHECK(lbc_read_header(file, sizeof(file), nullptr).code == ErrorCode::UnsupportedVersion);
}

static void test_rejects_truncated()
{
    const u8 payload[] = {1, 2, 3};
    u8 file[LBC_HEADER_SIZE + 3];
    build_file(file, payload, 3);
    CHECK(lbc_read_header(file, LBC_HEADER_SIZE - 1, nullptr).code == ErrorCode::CorruptBytecode);
    CHECK(lbc_read_header(file, sizeof(file) - 1, nullptr).code == ErrorCode::CorruptBytecode);
    CHECK(lbc_read_header(nullptr, 0, nullptr).code == ErrorCode::CorruptBytecode);
}

static void test_rejects_corrupt_payload()
{
    const u8 payload[] = {1, 2, 3, 4};
    u8 file[LBC_HEADER_SIZE + 4];
    build_file(file, payload, 4);
    file[LBC_HEADER_SIZE + 2] ^= 0xFF;
    CHECK(lbc_read_header(file, sizeof(file), nullptr).code == ErrorCode::ChecksumMismatch);
}

static void test_rejects_size_mismatch()
{
    const u8 payload[] = {1, 2, 3, 4};
    u8 file[LBC_HEADER_SIZE + 4];
    build_file(file, payload, 4);
    detail::write_u32(file + 12, 3);
    CHECK(lbc_read_header(file, sizeof(file), nullptr).code == ErrorCode::CorruptBytecode);
}

int main()
{
    test_crc32_known_vectors();
    test_roundtrip();
    test_empty_payload();
    test_rejects_bad_magic();
    test_rejects_future_version();
    test_rejects_truncated();
    test_rejects_corrupt_payload();
    test_rejects_size_mismatch();

    if (failures == 0)
    {
        printf("lbc: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "lbc: %d failure(s)\n", failures);
    return 1;
}
