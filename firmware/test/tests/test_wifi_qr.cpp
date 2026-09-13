// test_wifi_qr.cpp — Wi-Fi QR payload construction.
//
// The escaping rules are the point of these tests. A passphrase or SSID containing
// ';' ':' ',' '\\' or '"' must be backslash-escaped, or the scanner terminates the
// field early and joins the WRONG network with the WRONG password — a failure mode
// that looks like a broken QR code rather than a payload bug.
#include "net/wifi_qr.h"

#include <gtest/gtest.h>

#include <cstring>

TEST(WifiQr, SimplePayloadMatchesTheStandardFormat) {
    char out[CC_QR_PAYLOAD_MAX];
    const size_t n = cc_qrBuildWifiPayload(out, sizeof(out), "MyNet", "password1", "WPA");
    EXPECT_GT(n, 0u);
    EXPECT_STREQ(out, "WIFI:T:WPA;S:MyNet;P:password1;H:false;;");
}

TEST(WifiQr, ApCredentialsShapeAsUsedByThePortal) {
    // The exact shape the portal produces: "ChromaWOTD-<6 hex>" + 8 digits.
    char out[CC_QR_PAYLOAD_MAX];
    const size_t n = cc_qrBuildWifiPayload(out, sizeof(out), "ChromaWOTD-76B144", "26681037", "WPA");
    EXPECT_GT(n, 0u);
    EXPECT_STREQ(out, "WIFI:T:WPA;S:ChromaWOTD-76B144;P:26681037;H:false;;");
}

TEST(WifiQr, SemicolonInPassphraseIsEscaped) {
    // Un-escaped, this would end the P: field at "abc" and the scan would fail or
    // use a truncated password.
    char out[CC_QR_PAYLOAD_MAX];
    ASSERT_GT(cc_qrBuildWifiPayload(out, sizeof(out), "Net", "abc;def", "WPA"), 0u);
    EXPECT_STREQ(out, "WIFI:T:WPA;S:Net;P:abc\\;def;H:false;;");
}

TEST(WifiQr, ColonCommaQuoteAndBackslashAreEscaped) {
    char out[CC_QR_PAYLOAD_MAX];
    ASSERT_GT(cc_qrBuildWifiPayload(out, sizeof(out), "Net", "a:b,c\"d\\e", "WPA"), 0u);
    EXPECT_STREQ(out, "WIFI:T:WPA;S:Net;P:a\\:b\\,c\\\"d\\\\e;H:false;;");
}

TEST(WifiQr, SsidWithSpacesAndApostrophesPassesThroughUnescaped) {
    // Spaces and apostrophes are legal in an SSID and are NOT reserved by the format,
    // so escaping them would corrupt the value.
    char out[CC_QR_PAYLOAD_MAX];
    ASSERT_GT(cc_qrBuildWifiPayload(out, sizeof(out), "Bob's Wi-Fi", "pw", "WPA"), 0u);
    EXPECT_STREQ(out, "WIFI:T:WPA;S:Bob's Wi-Fi;P:pw;H:false;;");
}

TEST(WifiQr, OpenNetworkOmitsThePassphraseField) {
    char out[CC_QR_PAYLOAD_MAX];
    ASSERT_GT(cc_qrBuildWifiPayload(out, sizeof(out), "OpenNet", "", "nopass"), 0u);
    EXPECT_STREQ(out, "WIFI:T:nopass;S:OpenNet;H:false;;");
    EXPECT_EQ(strstr(out, "P:"), nullptr) << "an open network must not carry a P: field";
}

TEST(WifiQr, EmptySsidYieldsNoPayload) {
    // Without an SSID there is nothing to join, and the caller uses 0 as the signal
    // to skip drawing a code rather than emitting a useless one.
    char out[CC_QR_PAYLOAD_MAX];
    EXPECT_EQ(cc_qrBuildWifiPayload(out, sizeof(out), "", "pw", "WPA"), 0u);
    EXPECT_EQ(cc_qrBuildWifiPayload(out, sizeof(out), nullptr, "pw", "WPA"), 0u);
}

TEST(WifiQr, TooSmallBufferReturnsZeroRatherThanTruncating) {
    // A truncated payload scans as a DIFFERENT network, so it must never be emitted.
    char small[20];
    EXPECT_EQ(cc_qrBuildWifiPayload(small, sizeof(small), "MyNet", "password1", "WPA"), 0u);
    EXPECT_EQ(small[0], '\0') << "on failure the buffer must be left as an empty string";
}

TEST(WifiQr, MaxLengthCredentialsStillFit) {
    // Worst case the device allows: 32-char SSID and 63-char passphrase (WPA2 max).
    char ssid[33], pass[64];
    memset(ssid, 'S', 32);
    ssid[32] = '\0';
    memset(pass, 'p', 63);
    pass[63] = '\0';

    char out[CC_QR_PAYLOAD_MAX];
    const size_t n = cc_qrBuildWifiPayload(out, sizeof(out), ssid, pass, "WPA");
    EXPECT_GT(n, 0u) << "max-length credentials must still produce a payload";
    EXPECT_LT(n, CC_QR_PAYLOAD_MAX);
}

TEST(WifiQr, WorstCaseEscapingStillFits) {
    // Max lengths where EVERY character needs escaping (worst-case expansion 2x).
    char ssid[33], pass[64];
    memset(ssid, ';', 32);
    ssid[32] = '\0';
    memset(pass, ';', 63);
    pass[63] = '\0';

    char out[CC_QR_PAYLOAD_MAX];
    const size_t n = cc_qrBuildWifiPayload(out, sizeof(out), ssid, pass, "WPA");
    EXPECT_GT(n, 0u) << "worst-case escaping must still fit CC_QR_PAYLOAD_MAX";
}
