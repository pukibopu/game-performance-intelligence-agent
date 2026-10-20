#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "gpia/io/file_io.hpp"
#include "gpia/io/sha256.hpp"

TEST(Sha256, MatchesNistVectors)
{
    EXPECT_EQ(gpia::sha256_hex(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(gpia::sha256_hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(gpia::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, IncrementalUpdatesMatchOneShot)
{
    // 一百万个 a 分成大小不一的块喂入，覆盖跨块拼接的路径
    const std::string million(1'000'000, 'a');
    gpia::Sha256 hasher;
    std::size_t offset = 0;
    std::size_t step = 1;
    while (offset < million.size())
    {
        const std::size_t take = std::min(step, million.size() - offset);
        hasher.update(std::string_view(million).substr(offset, take));
        offset += take;
        step = step * 3 % 997 + 1;
    }
    std::string hex;
    for (const auto byte : hasher.finish())
    {
        constexpr std::string_view kHex = "0123456789abcdef";
        hex.push_back(kHex[byte >> 4]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    EXPECT_EQ(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    EXPECT_EQ(gpia::sha256_hex(million), hex);
}

TEST(Sha256, MatchesSchemaExampleManifest)
{
    const auto bytes = gpia::read_file_bytes(std::filesystem::path{GPIA_SCHEMAS_DIR} / "examples" /
                                             "frames.example.csv");
    ASSERT_TRUE(bytes);
    EXPECT_EQ(gpia::sha256_hex(*bytes),
              "da9f165eb080a621744cb6b402127f496f6d729dc7330395d0e6a7db2d1f8354");
}
