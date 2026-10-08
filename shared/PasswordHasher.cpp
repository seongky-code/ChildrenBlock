#include "pch.h"
#include "PasswordHasher.h"

namespace {

constexpr ULONG kSaltBytes = 16;
constexpr ULONG kHashBytes = 32;

CString ToHex(const std::vector<BYTE>& data) {
    CString hex;
    for (BYTE b : data) hex.AppendFormat(_T("%02x"), b);
    return hex;
}

bool FromHex(const CString& hex, std::vector<BYTE>& out) {
    if (hex.GetLength() % 2 != 0) return false;
    out.clear();
    for (int i = 0; i < hex.GetLength(); i += 2) {
        TCHAR pair[3] = {hex[i], hex[i + 1], 0};
        TCHAR* end = nullptr;
        unsigned long v = _tcstoul(pair, &end, 16);
        if (end != pair + 2) return false;
        out.push_back(static_cast<BYTE>(v));
    }
    return true;
}

bool Derive(const CString& password, const std::vector<BYTE>& salt, int iterations, std::vector<BYTE>& out) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, password, password.GetLength(), nullptr, 0, nullptr, nullptr);
    std::vector<BYTE> pw(len > 0 ? len : 1, 0);
    if (len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, password, password.GetLength(), reinterpret_cast<LPSTR>(pw.data()), len,
                            nullptr, nullptr);
    }

    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (BCRYPT_SUCCESS(status)) {
        out.assign(kHashBytes, 0);
        status = BCryptDeriveKeyPBKDF2(alg, pw.data(), static_cast<ULONG>(len > 0 ? len : 0),
                                       const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
                                       static_cast<ULONGLONG>(iterations), out.data(), kHashBytes, 0);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    SecureZeroMemory(pw.data(), pw.size());
    return BCRYPT_SUCCESS(status);
}

}  // namespace

namespace PasswordHasher {

bool Create(const CString& password, CString& saltHex, CString& hashHex, int& iterations) {
    std::vector<BYTE> salt(kSaltBytes);
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, salt.data(), kSaltBytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        return false;

    std::vector<BYTE> hash;
    if (!Derive(password, salt, kDefaultIterations, hash)) return false;

    saltHex = ToHex(salt);
    hashHex = ToHex(hash);
    iterations = kDefaultIterations;
    return true;
}

bool Verify(const CString& password, const CString& saltHex, const CString& hashHex, int iterations) {
    std::vector<BYTE> salt, expected, actual;
    if (iterations <= 0 || !FromHex(saltHex, salt) || !FromHex(hashHex, expected)) return false;
    if (!Derive(password, salt, iterations, actual) || actual.size() != expected.size()) return false;

    BYTE diff = 0;  // 상수 시간 비교
    for (size_t i = 0; i < actual.size(); ++i) diff |= actual[i] ^ expected[i];
    return diff == 0;
}

}  // namespace PasswordHasher
