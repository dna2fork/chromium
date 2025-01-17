// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/fido/mac/credential_metadata.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "components/cbor/cbor_reader.h"
#include "components/cbor/cbor_values.h"
#include "components/cbor/cbor_writer.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/hkdf.h"
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace device {
namespace fido {
namespace mac {

using cbor::CBORWriter;
using cbor::CBORReader;
using cbor::CBORValue;

namespace {

enum Algorithm : uint8_t {
  kAes256Gcm = 0,
  kHmacSha256 = 1,
};

// Derive keys from the caller-provided key to avoid using the same key for
// both algorithms.
std::string DeriveKey(base::StringPiece in, Algorithm alg) {
  static constexpr size_t kKeyLength = 32u;
  std::string key;
  const uint8_t info = static_cast<uint8_t>(alg);
  const bool hkdf_init = ::HKDF(
      reinterpret_cast<uint8_t*>(base::WriteInto(&key, kKeyLength + 1)),
      kKeyLength, EVP_sha256(), reinterpret_cast<const uint8_t*>(in.data()),
      in.size(), nullptr /* salt */, 0, &info, 1);
  DCHECK(hkdf_init);
  return key;
}

}  // namespace

CredentialMetadata::CredentialMetadata(const std::string& profile_id)
    : profile_id_(profile_id) {}
CredentialMetadata::~CredentialMetadata() = default;

CredentialMetadata::UserEntity::UserEntity(std::vector<uint8_t> id_,
                                           std::string name_,
                                           std::string display_name_)
    : id(std::move(id_)),
      name(std::move(name_)),
      display_name(std::move(display_name_)) {}
CredentialMetadata::UserEntity::UserEntity(
    const CredentialMetadata::UserEntity&) = default;
CredentialMetadata::UserEntity::UserEntity(CredentialMetadata::UserEntity&&) =
    default;
CredentialMetadata::UserEntity& CredentialMetadata::UserEntity::operator=(
    CredentialMetadata::UserEntity&&) = default;
CredentialMetadata::UserEntity::~UserEntity() = default;

static constexpr size_t kNonceLength = 12;

// static
base::Optional<std::vector<uint8_t>> CredentialMetadata::SealCredentialId(
    const std::string& profile_id,
    const std::string& rp_id,
    const UserEntity& user) {
  CredentialMetadata cryptor(profile_id);

  // The first 13 bytes are the version and nonce.
  std::vector<uint8_t> result(1 + kNonceLength);
  result[0] = kVersion;
  // Pick a random nonce. N.B. the nonce is similar to an IV. It needs to be
  // distinct (but not necessarily random). Nonce reuse breaks confidentiality
  // (in particular, it leaks the XOR of the plaintexts encrypted under the
  // same nonce and key).
  base::span<uint8_t> nonce(result.data() + 1, kNonceLength);
  RAND_bytes(nonce.data(), nonce.size());  // RAND_bytes always returns 1.

  // The remaining bytes are the CBOR-encoded UserEntity, encrypted with
  // AES-256-GCM and authenticated with the version and RP ID.
  CBORValue::ArrayValue cbor_user;
  cbor_user.emplace_back(CBORValue(user.id));
  cbor_user.emplace_back(CBORValue(user.name, CBORValue::Type::BYTE_STRING));
  cbor_user.emplace_back(
      CBORValue(user.display_name, CBORValue::Type::BYTE_STRING));
  base::Optional<std::vector<uint8_t>> pt =
      CBORWriter::Write(CBORValue(std::move(cbor_user)));
  if (!pt) {
    return base::nullopt;
  }
  base::Optional<std::string> ciphertext =
      cryptor.Seal(nonce, *pt, MakeAad(rp_id));
  if (!ciphertext) {
    return base::nullopt;
  }
  base::span<const char> cts(reinterpret_cast<const char*>(ciphertext->data()),
                             ciphertext->size());
  result.insert(result.end(), cts.begin(), cts.end());
  return result;
}

// static
base::Optional<CredentialMetadata::UserEntity>
CredentialMetadata::UnsealCredentialId(
    const std::string& profile_id,
    const std::string& rp_id,
    base::span<const uint8_t> credential_id) {
  CredentialMetadata cryptor(profile_id);

  // Recover the nonce and check for the correct version byte. Then try to
  // decrypt the remaining bytes.
  if (credential_id.size() <= 1 + kNonceLength ||
      credential_id[0] != kVersion) {
    return base::nullopt;
  }

  base::Optional<std::string> plaintext =
      cryptor.Unseal(credential_id.subspan(1, kNonceLength),
                     credential_id.subspan(1 + kNonceLength), MakeAad(rp_id));
  if (!plaintext) {
    return base::nullopt;
  }

  // The recovered plaintext should decode into the UserEntity struct.
  base::Optional<CBORValue> maybe_array = CBORReader::Read(base::make_span(
      reinterpret_cast<const uint8_t*>(plaintext->data()), plaintext->size()));
  if (!maybe_array || !maybe_array->is_array()) {
    return base::nullopt;
  }
  const CBORValue::ArrayValue& array = maybe_array->GetArray();
  if (array.size() != 3 || !array[0].is_bytestring() ||
      !array[1].is_bytestring() || !array[2].is_bytestring()) {
    return base::nullopt;
  }
  return UserEntity(array[0].GetBytestring(),
                    array[1].GetBytestringAsString().as_string(),
                    array[2].GetBytestringAsString().as_string());
}

// static
base::Optional<std::string> CredentialMetadata::EncodeRpIdAndUserId(
    const std::string& profile_id,
    const std::string& rp_id,
    base::span<const uint8_t> user_id) {
  // Encoding RP ID along with the user ID hides whether the same user ID was
  // reused on different RPs.
  const auto* user_id_data = reinterpret_cast<const char*>(user_id.data());
  return CredentialMetadata(profile_id)
      .HmacForStorage(rp_id + "/" +
                      std::string(user_id_data, user_id_data + user_id.size()));
}

// static
base::Optional<std::string> CredentialMetadata::EncodeRpId(
    const std::string& profile_id,
    const std::string& rp_id) {
  return CredentialMetadata(profile_id).HmacForStorage(rp_id);
}

// static
std::string CredentialMetadata::MakeAad(const std::string& rp_id) {
  return std::string(1, kVersion) + rp_id;
}

base::Optional<std::string> CredentialMetadata::Seal(
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> plaintext,
    base::StringPiece authenticated_data) const {
  const std::string key = DeriveKey(profile_id_, Algorithm::kAes256Gcm);
  crypto::Aead aead(crypto::Aead::AES_256_GCM);
  aead.Init(&key);
  std::string ciphertext;
  if (!aead.Seal(
          base::StringPiece(reinterpret_cast<const char*>(plaintext.data()),
                            plaintext.size()),
          base::StringPiece(reinterpret_cast<const char*>(nonce.data()),
                            nonce.size()),
          authenticated_data, &ciphertext)) {
    return base::nullopt;
  }
  return ciphertext;
}

base::Optional<std::string> CredentialMetadata::Unseal(
    base::span<const uint8_t> nonce,
    base::span<const uint8_t> ciphertext,
    base::StringPiece authenticated_data) const {
  const std::string key = DeriveKey(profile_id_, Algorithm::kAes256Gcm);
  crypto::Aead aead(crypto::Aead::AES_256_GCM);
  aead.Init(&key);
  std::string plaintext;
  if (!aead.Open(
          base::StringPiece(reinterpret_cast<const char*>(ciphertext.data()),
                            ciphertext.size()),
          base::StringPiece(reinterpret_cast<const char*>(nonce.data()),
                            nonce.size()),
          authenticated_data, &plaintext)) {
    return base::nullopt;
  }
  return plaintext;
}

base::Optional<std::string> CredentialMetadata::HmacForStorage(
    base::StringPiece data) const {
  crypto::HMAC hmac(crypto::HMAC::SHA256);
  const std::string key = DeriveKey(profile_id_, Algorithm::kHmacSha256);
  std::vector<uint8_t> digest(hmac.DigestLength());
  if (!hmac.Init(key) || !hmac.Sign(data, digest.data(), hmac.DigestLength())) {
    return base::nullopt;
  }
  // The keychain fields that store RP ID and User ID seem to only accept
  // NSString (not NSData), so we HexEncode to ensure the result to be
  // UTF-8-decodable.
  return base::HexEncode(digest.data(), digest.size());
}

}  // namespace mac
}  // namespace fido
}  // namespace device
