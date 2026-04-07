#include "message_crypto.h"

#if MESSAGE_CRYPTO_AVAILABLE

// ── Key derivation ────────────────────────────────────────────
void MessageCrypto::deriveKeys(const char* password) {
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);
    mbedtls_sha256_update(&ctx, reinterpret_cast<const uint8_t*>(password), strlen(password));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    memcpy(aes_key_,  hash,      16);
    memcpy(hmac_key_, hash,      32);
}

// ── HMAC-SHA256 ───────────────────────────────────────────────
void MessageCrypto::computeHmac(const uint8_t* data, size_t len,
                                 uint8_t out32[32]) const {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_setup(&ctx, info, /*hmac=*/1);
    mbedtls_md_hmac_starts(&ctx, hmac_key_, 32);
    mbedtls_md_hmac_update(&ctx, data, len);
    mbedtls_md_hmac_finish(&ctx, out32);
    mbedtls_md_free(&ctx);
}

// ── AES-128-CTR ───────────────────────────────────────────────
bool MessageCrypto::aesCtr(const uint8_t nonce16[16],
                            uint8_t* buf, size_t len) const {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, aes_key_, 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    uint8_t nonce_copy[16];
    memcpy(nonce_copy, nonce16, 16);
    uint8_t stream_block[16] = {};
    size_t  nc_off = 0;
    mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce_copy, stream_block, buf, buf);
    mbedtls_aes_free(&ctx);
    return true;
}

// ── Hex helpers ───────────────────────────────────────────────
String MessageCrypto::toHex(const uint8_t* data, size_t len) {
    String s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        char h[3];
        snprintf(h, sizeof(h), "%02x", data[i]);
        s += h;
    }
    return s;
}

bool MessageCrypto::fromHex(const char* hex, uint8_t* out, size_t out_len) {
    if (!hex || strlen(hex) != out_len * 2) return false;
    for (size_t i = 0; i < out_len; ++i) {
        char b[3] = { hex[i*2], hex[i*2+1], '\0' };
        char* end;
        out[i] = static_cast<uint8_t>(strtoul(b, &end, 16));
        if (*end) return false;
    }
    return true;
}

// ── Public API ────────────────────────────────────────────────
MessageCrypto::MessageCrypto(const char* device_pass) {
    deriveKeys(device_pass);
}

String MessageCrypto::encryptEnvelope(const String& plaintext) {
    // 1. Random 16-byte nonce from ESP32 hardware RNG
    uint8_t nonce[16];
    for (int i = 0; i < 16; i += 4) {
        uint32_t r = esp_random();
        memcpy(nonce + i, &r, 4);
    }

    // 2. Encrypt plaintext in-place
    size_t len = plaintext.length();
    uint8_t* buf = new uint8_t[len];
    memcpy(buf, plaintext.c_str(), len);
    if (!aesCtr(nonce, buf, len)) {
        delete[] buf;
        return "";
    }

    // 3. Build "enc" = hex(nonce || ciphertext)
    const String enc = toHex(nonce, 16) + toHex(buf, len);
    delete[] buf;

    // 4. Timestamp (device uptime ms — binds HMAC to this session)
    char ts[16];
    snprintf(ts, sizeof(ts), "%lu", static_cast<unsigned long>(millis()));

    // 5. HMAC over "<ts>:<enc>"
    const String mac_input = String(ts) + ":" + enc;
    uint8_t sig[32];
    computeHmac(reinterpret_cast<const uint8_t*>(mac_input.c_str()),
                mac_input.length(), sig);

    return String(ts) + ":" + enc + ":" + toHex(sig, 32);
}

String MessageCrypto::decryptEnvelope(const String& envelope) {
    // Parse "<ts>:<enc>:<sig>"
    const int first = envelope.indexOf(':');
    const int last  = envelope.lastIndexOf(':');
    if (first < 0 || last == first) return "";

    const String ts_str  = envelope.substring(0, first);
    const String enc     = envelope.substring(first + 1, last);
    const String sig_hex = envelope.substring(last + 1);

    // Verify HMAC
    const String mac_input = ts_str + ":" + enc;
    uint8_t expected[32];
    computeHmac(reinterpret_cast<const uint8_t*>(mac_input.c_str()),
                mac_input.length(), expected);
    uint8_t received[32];
    if (!fromHex(sig_hex.c_str(), received, 32)) return "";
    // Constant-time compare to avoid timing attacks
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i) diff |= expected[i] ^ received[i];
    if (diff != 0) return "";

    // Extract nonce (first 32 hex chars = 16 bytes) and ciphertext
    if (enc.length() < 32) return "";
    uint8_t nonce[16];
    if (!fromHex(enc.substring(0, 32).c_str(), nonce, 16)) return "";

    const size_t cipher_hex_len = enc.length() - 32;
    if (cipher_hex_len % 2 != 0) return "";
    const size_t cipher_len = cipher_hex_len / 2;
    uint8_t* buf = new uint8_t[cipher_len];
    if (!fromHex(enc.substring(32).c_str(), buf, cipher_len)) {
        delete[] buf;
        return "";
    }

    // Decrypt
    if (!aesCtr(nonce, buf, cipher_len)) {
        delete[] buf;
        return "";
    }

    String result;
    result.reserve(cipher_len);
    for (size_t i = 0; i < cipher_len; ++i) result += static_cast<char>(buf[i]);
    delete[] buf;
    return result;
}

#else  // ── Desktop stub (no mbedtls) ─────────────────────────

MessageCrypto::MessageCrypto(const char* /*device_pass*/) {}

String MessageCrypto::encryptEnvelope(const String& plaintext) {
    return plaintext; // passthrough — no crypto on host builds
}

String MessageCrypto::decryptEnvelope(const String& envelope) {
    return envelope;  // passthrough
}

#endif
