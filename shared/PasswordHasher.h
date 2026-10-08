#pragma once

// PBKDF2-HMAC-SHA256 (Windows CNG). 비밀번호는 평문으로 저장하지 않는다.
namespace PasswordHasher {

constexpr int kDefaultIterations = 200000;
constexpr int kMinLength = 4;

bool Create(const CString& password, CString& saltHex, CString& hashHex, int& iterations);
bool Verify(const CString& password, const CString& saltHex, const CString& hashHex, int iterations);

}  // namespace PasswordHasher
