// Minecraft online-mode authentication & protocol encryption (OpenSSL).
#pragma once
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

namespace cppfm::crypto {

using Bytes = std::vector<std::uint8_t>;

struct RsaKeyPair {
    EVP_PKEY* pkey = nullptr;
    Bytes publicDer;   // X.509 SubjectPublicKeyInfo (what MC expects)

    void generate() {
        if (pkey) { EVP_PKEY_free(pkey); pkey = nullptr; }
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0 ||
            EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 1024) <= 0 ||
            EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            if (ctx) EVP_PKEY_CTX_free(ctx);
            throw std::runtime_error("RSA keygen failed");
        }
        EVP_PKEY_CTX_free(ctx);
        // export SPKI DER
        int len = i2d_PUBKEY(pkey, nullptr);
        publicDer.resize(len);
        auto* pp = publicDer.data();
        i2d_PUBKEY(pkey, &pp);
    }
    RsaKeyPair() = default;
    ~RsaKeyPair() { if (pkey) EVP_PKEY_free(pkey); }
    RsaKeyPair(const RsaKeyPair&) = delete;
    RsaKeyPair& operator=(const RsaKeyPair&) = delete;
};


inline bool verifyRsaSha256(const Bytes& pubDer, const std::uint8_t* data, std::size_t len, const Bytes& sig) {
    if (pubDer.empty() || sig.empty()) return false;
    const unsigned char* pp = pubDer.data();
    EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &pp, (long)pubDer.size());
    if (!pkey) return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        if (EVP_DigestVerifyUpdate(ctx, data, len) == 1) {
            ok = EVP_DigestVerifyFinal(ctx, sig.data(), sig.size()) == 1;
        }
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

inline Bytes rsaDecryptP(EVP_PKEY* kp, const std::uint8_t* ct, std::size_t n) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(kp, nullptr);
    if (!ctx) throw std::runtime_error("ctx");
    if (EVP_PKEY_decrypt_init(ctx) <= 0) throw std::runtime_error("dec init");
    std::size_t outl = 0;
    if (EVP_PKEY_decrypt(ctx, nullptr, &outl, ct, n) <= 0)
        { EVP_PKEY_CTX_free(ctx); throw std::runtime_error("dec size"); }
    Bytes pt(outl);
    if (EVP_PKEY_decrypt(ctx, pt.data(), &outl, ct, n) <= 0)
        { EVP_PKEY_CTX_free(ctx); throw std::runtime_error("decrypt"); }
    EVP_PKEY_CTX_free(ctx);
    pt.resize(outl);
    return pt;
}

// Minecraft's serverId hash: SHA1(serverId || sharedSecret || publicKey),
// rendered as a Java BigInteger.toString(16)-style hex string.
inline std::string mcSha1Hex(const std::string& serverId,
                             const Bytes& sharedSecret,
                             const Bytes& publicKey) {
    std::uint8_t md[20];
    unsigned int mdlen = 0;
    EVP_MD_CTX* m = EVP_MD_CTX_new();
    EVP_DigestInit_ex(m, EVP_sha1(), nullptr);
    EVP_DigestUpdate(m, serverId.data(), serverId.size());
    EVP_DigestUpdate(m, sharedSecret.data(), sharedSecret.size());
    EVP_DigestUpdate(m, publicKey.data(), publicKey.size());
    EVP_DigestFinal_ex(m, md, &mdlen);
    EVP_MD_CTX_free(m);

    BIGNUM* bn = BN_bin2bn(md, 20, nullptr);
    char* hex = BN_bn2hex(bn);
    std::string s(hex);
    OPENSSL_free(hex);
    BN_free(bn);
    for (auto& ch : s) ch = static_cast<char>(std::tolower((unsigned char)ch));
    // BigInteger.toString strips leading zeros but keeps sign via '-'
    while (s.size() > 1 && s[0] == '0') s.erase(s.begin());
    return s;
}

// AES-128/CFB8 persistent cipher contexts (one per direction).
struct AesCfb8 {
    EVP_CIPHER_CTX* ctx = nullptr;

    void initEncrypt(const Bytes& key) { init(key, true); }
    void initDecrypt(const Bytes& key) { init(key, false); }
private:
    void init(const Bytes& key, bool enc) {
        ctx = EVP_CIPHER_CTX_new();
        const std::uint8_t* k = key.data();
        EVP_CipherInit_ex(ctx, EVP_aes_128_cfb8(), nullptr,
                          k, k, enc ? 1 : 0);
    }
public:
    void crypt(const std::uint8_t* in, std::size_t n, std::uint8_t* out) {
        int outl = 0;
        EVP_CipherUpdate(ctx, out, &outl, in, static_cast<int>(n));
    }
    ~AesCfb8() { if (ctx) EVP_CIPHER_CTX_free(ctx); }
};

} // namespace cppfm::crypto
