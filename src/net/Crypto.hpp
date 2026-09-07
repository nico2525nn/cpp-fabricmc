// Minecraft online-mode authentication & protocol encryption (OpenSSL).
#pragma once
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
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
        if (!ctx) throw std::runtime_error("RSA keygen context allocation failed");
        EVP_PKEY* generated = nullptr;
        if (EVP_PKEY_keygen_init(ctx) <= 0 ||
            EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 1024) <= 0 ||
            EVP_PKEY_keygen(ctx, &generated) <= 0) {
            if (generated) EVP_PKEY_free(generated);
            EVP_PKEY_CTX_free(ctx);
            throw std::runtime_error("RSA keygen failed");
        }
        EVP_PKEY_CTX_free(ctx);
        pkey = generated;
        // export SPKI DER
        int len = i2d_PUBKEY(pkey, nullptr);
        if (len <= 0) {
            EVP_PKEY_free(pkey);
            pkey = nullptr;
            throw std::runtime_error("RSA public key export failed");
        }
        publicDer.resize(static_cast<std::size_t>(len));
        auto* pp = publicDer.data();
        if (i2d_PUBKEY(pkey, &pp) != len) {
            EVP_PKEY_free(pkey);
            pkey = nullptr;
            publicDer.clear();
            throw std::runtime_error("RSA public key export failed");
        }
    }
    RsaKeyPair() = default;
    ~RsaKeyPair() { if (pkey) EVP_PKEY_free(pkey); }
    RsaKeyPair(const RsaKeyPair&) = delete;
    RsaKeyPair& operator=(const RsaKeyPair&) = delete;
};


inline bool verifyRsaSha256(const Bytes& pubDer, const std::uint8_t* data, std::size_t len, const Bytes& sig) {
    if (pubDer.empty() || sig.empty() || (len != 0 && data == nullptr) ||
        pubDer.size() > static_cast<std::size_t>(std::numeric_limits<long>::max())) return false;
    const unsigned char* pp = pubDer.data();
    EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &pp, (long)pubDer.size());
    if (!pkey) return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx && EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        if (EVP_DigestVerifyUpdate(ctx, data, len) == 1) {
            ok = EVP_DigestVerifyFinal(ctx, sig.data(), sig.size()) == 1;
        }
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

inline Bytes rsaDecryptP(EVP_PKEY* kp, const std::uint8_t* ct, std::size_t n) {
    if (!kp || (n != 0 && !ct)) throw std::invalid_argument("invalid RSA decrypt input");
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new(kp, nullptr), &EVP_PKEY_CTX_free);
    if (!ctx) throw std::runtime_error("RSA decrypt context allocation failed");
    if (EVP_PKEY_decrypt_init(ctx.get()) <= 0) throw std::runtime_error("RSA decrypt init failed");
    std::size_t outl = 0;
    if (EVP_PKEY_decrypt(ctx.get(), nullptr, &outl, ct, n) <= 0 || outl == 0)
        throw std::runtime_error("RSA decrypt size query failed");
    Bytes pt(outl);
    if (EVP_PKEY_decrypt(ctx.get(), pt.data(), &outl, ct, n) <= 0)
        throw std::runtime_error("RSA decrypt failed");
    pt.resize(outl);
    return pt;
}

// Minecraft's serverId hash: SHA1(serverId || sharedSecret || publicKey), rendered as a Java BigInteger.toString(16)-style hex string.
inline std::string mcSha1Hex(const std::string& serverId,
                             const Bytes& sharedSecret,
                             const Bytes& publicKey) {
    std::uint8_t md[20];
    unsigned int mdlen = 0;
    EVP_MD_CTX* m = EVP_MD_CTX_new();
    if (!m) throw std::runtime_error("SHA-1 context allocation failed");
    if (EVP_DigestInit_ex(m, EVP_sha1(), nullptr) != 1 ||
        EVP_DigestUpdate(m, serverId.data(), serverId.size()) != 1 ||
        EVP_DigestUpdate(m, sharedSecret.data(), sharedSecret.size()) != 1 ||
        EVP_DigestUpdate(m, publicKey.data(), publicKey.size()) != 1 ||
        EVP_DigestFinal_ex(m, md, &mdlen) != 1 || mdlen != sizeof(md)) {
        EVP_MD_CTX_free(m);
        throw std::runtime_error("SHA-1 digest failed");
    }
    EVP_MD_CTX_free(m);

    const bool negative = (md[0] & 0x80u) != 0;
    Bytes magnitude(md, md + sizeof(md));
    if (negative) {
        for (auto& byte : magnitude) byte = static_cast<std::uint8_t>(~byte);
        for (std::size_t i = magnitude.size(); i-- > 0;) {
            if (++magnitude[i] != 0) break;
        }
    }
    BIGNUM* bn = BN_bin2bn(magnitude.data(), static_cast<int>(magnitude.size()), nullptr);
    if (!bn) throw std::runtime_error("SHA-1 integer conversion failed");
    char* hex = BN_bn2hex(bn);
    if (!hex) { BN_free(bn); throw std::runtime_error("SHA-1 hex conversion failed"); }
    std::string s(hex);
    OPENSSL_free(hex);
    BN_free(bn);
    for (auto& ch : s) ch = static_cast<char>(std::tolower((unsigned char)ch));
    // Java's new BigInteger(digest).toString(16) uses a signed two's
    // complement byte array, unlike OpenSSL's unsigned BN_bin2bn.
    while (s.size() > 1 && s[0] == '0') s.erase(s.begin());
    if (negative && s != "0") s.insert(s.begin(), '-');
    return s;
}

// AES-128/CFB8 persistent cipher contexts (one per direction).
struct AesCfb8 {
    EVP_CIPHER_CTX* ctx = nullptr;

    void initEncrypt(const Bytes& key) { init(key, true); }
    void initDecrypt(const Bytes& key) { init(key, false); }
private:
    void init(const Bytes& key, bool enc) {
        if (key.size() != 16) throw std::invalid_argument("AES-128 requires a 16-byte key");
        if (ctx) { EVP_CIPHER_CTX_free(ctx); ctx = nullptr; }
        ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("AES cipher context allocation failed");
        const std::uint8_t* k = key.data();
        if (EVP_CipherInit_ex(ctx, EVP_aes_128_cfb8(), nullptr,
                              k, k, enc ? 1 : 0) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            ctx = nullptr;
            throw std::runtime_error("AES cipher initialization failed");
        }
    }
public:
    void crypt(const std::uint8_t* in, std::size_t n, std::uint8_t* out) {
        if (!ctx) throw std::logic_error("AES cipher is not initialized");
        if (n != 0 && (!in || !out)) throw std::invalid_argument("AES buffer is null");
        if (n > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("AES buffer is too large");
        if (n == 0) return;
        int outl = 0;
        if (EVP_CipherUpdate(ctx, out, &outl, in, static_cast<int>(n)) != 1 ||
            outl != static_cast<int>(n))
            throw std::runtime_error("AES cipher update failed");
    }
    ~AesCfb8() { if (ctx) EVP_CIPHER_CTX_free(ctx); }
};

} // namespace cppfm::crypto
