#pragma once

#include <cstdint>
#include <cstring>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

// AES-128-CTR + HMAC-SHA256 authenticated encryption.
// Keys are derived from the device password via SHA-256:
//   aes_key  = sha256(password)[0:16]
//   hmac_key = sha256(password)[0:32]
//
// Envelope format (plain ASCII string):
//   "<ts>:<hex(nonce16||ciphertext)>:<hex(hmac32)>"
//
// Uses mbedtls, which is bundled with the ESP32 Arduino core.
// On non-ESP32 (desktop tests) the methods are no-op passthroughs.

#if __has_include(<mbedtls/md.h>) && __has_include(<mbedtls/aes.h>)
#  define MESSAGE_CRYPTO_AVAILABLE 1
#  include <mbedtls/md.h>
#  include <mbedtls/aes.h>
#  include <mbedtls/sha256.h>
#else
#  define MESSAGE_CRYPTO_AVAILABLE 0
#endif

class MessageCrypto {
public:
    explicit MessageCrypto(const char* device_pass);

    // Encrypt plaintext -> "ts:enc:sig" envelope.  Returns "" on error.
    String encryptEnvelope(const String& plaintext);

    // Decrypt "ts:enc:sig" -> plaintext.  Returns "" on HMAC failure or bad format.
    String decryptEnvelope(const String& envelope);

private:
#if MESSAGE_CRYPTO_AVAILABLE
    uint8_t aes_key_[16];
    uint8_t hmac_key_[32];

    void    deriveKeys(const char* password);
    void    computeHmac(const uint8_t* data, size_t len, uint8_t out32[32]) const;
    bool    aesCtr(const uint8_t nonce16[16], uint8_t* buf, size_t len) const;

    static String toHex  (const uint8_t* data, size_t len);
    static bool   fromHex(const char* hex, uint8_t* out, size_t out_len);
#endif
};
