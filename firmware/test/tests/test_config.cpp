// test_config.cpp — device configuration: defaults, validation, coordinate parsing,
// NVS round-trip and the AP credential generation rule.
//
// These are the pieces that decide whether a device can connect at all, so they are
// tested rather than eyeballed. The NVS layer is a host stub (see
// config_nvs_host.cpp) with the same save/load contract as the device.
#include "config/config.h"
#include "config/config_active.h"
#include "net/portal.h"

#include <gtest/gtest.h>

#include <cstring>

namespace {
DeviceConfig fresh() {
    DeviceConfig c{};
    cc_configDefaults(&c);
    return c;
}
}  // namespace

// ------------------------------------------------------------- defaults -----

TEST(Config, DefaultsAreUsable) {
    DeviceConfig c = fresh();
    // The built-in timezone must be non-empty; otherwise the device would have no
    // time base at all on a fresh clone.
    EXPECT_TRUE(c.timezone[0] != '\0');
    // Coordinates must be inside the valid ranges (a bad default would be silently
    // rejected by validation and the portal would be unreachable-looking).
    EXPECT_GE(c.latitude, -90.0f);
    EXPECT_LE(c.latitude, 90.0f);
    EXPECT_GE(c.longitude, -180.0f);
    EXPECT_LE(c.longitude, 180.0f);
    EXPECT_EQ(c.contentMode, 0u);
}

TEST(Config, ProvisionedRequiresSsid) {
    DeviceConfig c = fresh();
    c.ssid[0] = '\0';
    EXPECT_FALSE(cc_configIsProvisioned(c));
    cc_configCopy(c.ssid, sizeof(c.ssid), "MyNetwork");
    EXPECT_TRUE(cc_configIsProvisioned(c));
}

// --------------------------------------------------------- copy bounds ------

TEST(Config, CopyIsAlwaysTerminatedAndBounded) {
    char dst[8];
    // Overlong source must be truncated, never overflow, and stay NUL-terminated.
    EXPECT_EQ(cc_configCopy(dst, sizeof(dst), "0123456789ABCDEF"), 7u);
    EXPECT_EQ(strlen(dst), 7u);
    EXPECT_STREQ(dst, "0123456");

    // Exactly-fitting source.
    EXPECT_EQ(cc_configCopy(dst, sizeof(dst), "1234567"), 7u);
    EXPECT_STREQ(dst, "1234567");

    // Empty and null sources both yield an empty string, not garbage.
    cc_configCopy(dst, sizeof(dst), "");
    EXPECT_STREQ(dst, "");
    cc_configCopy(dst, sizeof(dst), nullptr);
    EXPECT_STREQ(dst, "");
}

// ---------------------------------------------------- coordinate parsing ----

TEST(Config, ParseCoordAcceptsValidNumbers) {
    float v = 0;
    EXPECT_TRUE(cc_configParseCoord("-37.8528", &v, -90.0f, 90.0f));
    EXPECT_NEAR(v, -37.8528f, 0.001f);
    EXPECT_TRUE(cc_configParseCoord("145.1633", &v, -180.0f, 180.0f));
    EXPECT_NEAR(v, 145.1633f, 0.001f);
    EXPECT_TRUE(cc_configParseCoord("0.5", &v, -90.0f, 90.0f));
    // Surrounding whitespace is tolerated (a form may submit it).
    EXPECT_TRUE(cc_configParseCoord("  -33.9  ", &v, -90.0f, 90.0f));
    EXPECT_NEAR(v, -33.9f, 0.001f);
}

TEST(Config, ParseCoordRejectsGarbageAndOutOfRange) {
    float v = 0;
    EXPECT_FALSE(cc_configParseCoord("", &v, -90.0f, 90.0f));
    EXPECT_FALSE(cc_configParseCoord("abc", &v, -90.0f, 90.0f));
    EXPECT_FALSE(cc_configParseCoord("-37abc", &v, -90.0f, 90.0f)) << "trailing garbage accepted";
    EXPECT_FALSE(cc_configParseCoord("91", &v, -90.0f, 90.0f)) << "latitude above range";
    EXPECT_FALSE(cc_configParseCoord("-91", &v, -90.0f, 90.0f));
    EXPECT_FALSE(cc_configParseCoord("181", &v, -180.0f, 180.0f)) << "longitude above range";
    EXPECT_FALSE(cc_configParseCoord(nullptr, &v, -90.0f, 90.0f));
}

// ----------------------------------------------------------- validation -----

TEST(Config, ValidationAcceptsAGoodConfiguration) {
    DeviceConfig c = fresh();
    cc_configCopy(c.ssid, sizeof(c.ssid), "MyNetwork");
    cc_configCopy(c.passphrase, sizeof(c.passphrase), "hunter2hunter2");
    char err[160];
    EXPECT_TRUE(cc_configValidate(c, err, sizeof(err))) << err;
    EXPECT_STREQ(err, "");
}

TEST(Config, ValidationRequiresSsid) {
    DeviceConfig c = fresh();
    c.ssid[0] = '\0';
    char err[160];
    EXPECT_FALSE(cc_configValidate(c, err, sizeof(err)));
    EXPECT_NE(std::string(err).find("network name"), std::string::npos) << err;
}

TEST(Config, ValidationRejectsBothCoordsZero) {
    // (0,0) is the classic "field left blank" value and lands in the Atlantic, so it
    // is rejected rather than silently producing a plausible-but-wrong forecast.
    DeviceConfig c = fresh();
    cc_configCopy(c.ssid, sizeof(c.ssid), "MyNetwork");
    c.latitude = 0.0f;
    c.longitude = 0.0f;
    char err[160];
    EXPECT_FALSE(cc_configValidate(c, err, sizeof(err)));
    EXPECT_NE(std::string(err).find("cannot both be 0"), std::string::npos) << err;

    // Just one of them being zero is fine (the equator / Greenwich meridian).
    c.latitude = 0.0f;
    c.longitude = 12.0f;
    EXPECT_TRUE(cc_configValidate(c, err, sizeof(err))) << err;
}

TEST(Config, ValidationRejectsOutOfRangeCoords) {
    DeviceConfig c = fresh();
    cc_configCopy(c.ssid, sizeof(c.ssid), "MyNetwork");
    char err[160];
    c.latitude = 90.5f;
    EXPECT_FALSE(cc_configValidate(c, err, sizeof(err)));
    c.latitude = 10.0f;
    c.longitude = -180.5f;
    EXPECT_FALSE(cc_configValidate(c, err, sizeof(err)));
}

TEST(Config, ValidationAllowsEmptyPassphraseForOpenNetwork) {
    DeviceConfig c = fresh();
    cc_configCopy(c.ssid, sizeof(c.ssid), "OpenNetwork");
    c.passphrase[0] = '\0';
    char err[160];
    EXPECT_TRUE(cc_configValidate(c, err, sizeof(err))) << err;
}

TEST(Config, ValidationRejectsBadContentMode) {
    DeviceConfig c = fresh();
    cc_configCopy(c.ssid, sizeof(c.ssid), "MyNetwork");
    c.contentMode = 3;
    char err[160];
    EXPECT_FALSE(cc_configValidate(c, err, sizeof(err)));
}

// ------------------------------------------------------- NVS round-trip -----

TEST(Config, NvsRoundTripPreservesValues) {
    DeviceConfig saved = fresh();
    cc_configCopy(saved.ssid, sizeof(saved.ssid), "RoundTripNet");
    cc_configCopy(saved.passphrase, sizeof(saved.passphrase), "s3cret-passphrase");
    cc_configCopy(saved.timezone, sizeof(saved.timezone), "UTC0");
    cc_configCopy(saved.hostname, sizeof(saved.hostname), "StudyClock");
    saved.latitude = -27.4698f;
    saved.longitude = 153.0251f;
    saved.contentMode = 2;
    ASSERT_TRUE(cc_configSaveToNvs(saved));

    // Load over a fresh default struct: the stored values must win.
    DeviceConfig loaded = fresh();
    EXPECT_TRUE(cc_configLoadFromNvs(&loaded));
    EXPECT_STREQ(loaded.ssid, "RoundTripNet");
    EXPECT_STREQ(loaded.passphrase, "s3cret-passphrase");
    EXPECT_STREQ(loaded.timezone, "UTC0");
    EXPECT_STREQ(loaded.hostname, "StudyClock");
    EXPECT_NEAR(loaded.latitude, -27.4698f, 0.0001f);
    EXPECT_NEAR(loaded.longitude, 153.0251f, 0.0001f);
    EXPECT_EQ(loaded.contentMode, 2u);

    // Erase must make the store empty again (factory reset).
    ASSERT_TRUE(cc_configEraseNvs());
    DeviceConfig after = fresh();
    EXPECT_FALSE(cc_configLoadFromNvs(&after)) << "erase left values behind";
}

// --------------------------------------------------- portal credentials -----

TEST(Portal, ApNameIsDerivedFromTheMac) {
    PortalInfo info;
    cc_portalMakeInfo(0xABCDEF, 12345u, &info);
    EXPECT_STREQ(info.apName, "ChromaWOTD-ABCDEF");
}

TEST(Portal, PasswordIsLongEnoughForWpa2AndUsesTheSafeAlphabet) {
    PortalInfo a, b;
    cc_portalMakeInfo(0x112233, 0xDEADBEEFu, &a);
    cc_portalMakeInfo(0x112233, 0xFEEDFACEu, &b);

    // WPA2 requires >= 8 characters.
    EXPECT_GE(strlen(a.apPassword), 8u);
    // Different RNG seeds must give different passwords (per-boot randomness).
    EXPECT_STRNE(a.apPassword, b.apPassword);

    // Only the unambiguous alphabet: a user transcribes this from a 4-colour panel,
    // so O/0 and I/1/l must never appear.
    for (const char* p = a.apPassword; *p; p++) {
        EXPECT_TRUE((*p >= 'A' && *p <= 'Z') || (*p >= '2' && *p <= '9')) << *p;
        EXPECT_EQ(strchr("O0I1L", *p), nullptr) << "ambiguous glyph in password: " << *p;
    }
}

TEST(Portal, PasswordIsStableForTheSameSeed) {
    // Same seed => same password (the rule is deterministic; randomness comes from
    // the caller's esp_random()).
    PortalInfo a, b;
    cc_portalMakeInfo(1, 42u, &a);
    cc_portalMakeInfo(1, 42u, &b);
    EXPECT_STREQ(a.apPassword, b.apPassword);
}
