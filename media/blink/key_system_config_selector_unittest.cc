// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/strings/pattern.h"
#include "media/base/cdm_config.h"
#include "media/base/eme_constants.h"
#include "media/base/key_systems.h"
#include "media/base/media_permission.h"
#include "media/base/mime_util.h"
#include "media/blink/key_system_config_selector.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/web_encrypted_media_types.h"
#include "third_party/blink/public/platform/web_media_key_system_configuration.h"
#include "third_party/blink/public/platform/web_string.h"

namespace media {

namespace {

using blink::WebEncryptedMediaInitDataType;
using blink::WebEncryptedMediaSessionType;
using blink::WebMediaKeySystemConfiguration;
using blink::WebMediaKeySystemMediaCapability;
using blink::WebString;
using MediaKeysRequirement = WebMediaKeySystemConfiguration::Requirement;

// Key system strings. Clear Key support is hardcoded in KeySystemConfigSelector
// so kClearKeyKeySystem is the real key system string. The rest key system
// strings are for testing purpose only.
const char kClearKeyKeySystem[] = "org.w3.clearkey";
const char kSupportedKeySystem[] = "keysystem.test.supported";
const char kUnsupportedKeySystem[] = "keysystem.test.unsupported";

// Robustness strings for kSupportedKeySystem.
const char kSupportedRobustness[] = "supported";
const char kRecommendIdentifierRobustness[] = "recommend_identifier";
const char kRequireIdentifierRobustness[] = "require_identifier";
const char kDisallowHwSecureCodecRobustness[] = "disallow_hw_secure_codec";
const char kRequireHwSecureCodecRobustness[] = "require_hw_secure_codec";
const char kUnsupportedRobustness[] = "unsupported";

// Test container mime types. Supported types are prefixed with audio/video so
// that the test can perform EmeMediaType check.
const char kSupportedVideoContainer[] = "video/supported";
const char kSupportedAudioContainer[] = "audio/supported";
const char kUnsupportedContainer[] = "video/unsupported";
const char kInvalidContainer[] = "video/invalid";

// The codec strings. Supported types are prefixed with audio/video so
// that the test can perform EmeMediaType check.
// TODO(sandersd): Extended codec variants (requires proprietary codec support).
// TODO(xhwang): Platform Opus is not available on all Android versions, where
// some encrypted Opus related tests may fail. See PlatformHasOpusSupport()
// for more details.
const char kSupportedAudioCodec[] = "audio_codec";
const char kSupportedVideoCodec[] = "video_codec";
const char kUnsupportedCodec[] = "unsupported_codec";
const char kInvalidCodec[] = "foo";
const char kRequireHwSecureCodec[] = "require_hw_secure_codec";
const char kDisallowHwSecureCodec[] = "disallow_hw_secure_codec";
const char kExtendedVideoCodec[] = "video_extended_codec.extended";
const char kExtendedVideoCodecStripped[] = "video_extended_codec";
// A special codec that is supported by the key systems, but is not supported
// in IsSupportedMediaType() when |use_aes_decryptor| is true.
const char kUnsupportedByAesDecryptorCodec[] = "unsupported_by_aes_decryptor";

WebString MakeCodecs(const std::string& a, const std::string& b) {
  return WebString::FromUTF8(a + "," + b);
}

WebString GetSupportedVideoCodecs() {
  return MakeCodecs(kSupportedVideoCodec, kSupportedVideoCodec);
}

WebString GetSubsetSupportedVideoCodecs() {
  return MakeCodecs(kSupportedVideoCodec, kUnsupportedCodec);
}

WebString GetSubsetInvalidVideoCodecs() {
  return MakeCodecs(kSupportedVideoCodec, kInvalidCodec);
}

bool IsValidContainerMimeType(const std::string& container_mime_type) {
  return container_mime_type != kInvalidContainer;
}

bool IsValidCodec(const std::string& codec) {
  return codec != kInvalidCodec;
}

// Returns whether |type| is compatible with |media_type|.
bool IsCompatibleWithEmeMediaType(EmeMediaType media_type,
                                  const std::string& type) {
  if (media_type == EmeMediaType::AUDIO && base::MatchPattern(type, "video*"))
    return false;

  if (media_type == EmeMediaType::VIDEO && base::MatchPattern(type, "audio*"))
    return false;

  return true;
}

// Pretend that we support all |container_mime_type| and |codecs| except for
// those explicitly marked as invalid.
bool IsSupportedMediaType(const std::string& container_mime_type,
                          const std::string& codecs,
                          bool use_aes_decryptor) {
  if (container_mime_type == kInvalidContainer)
    return false;

  std::vector<std::string> codec_vector;
  SplitCodecsToVector(codecs, &codec_vector, false);
  for (const std::string& codec : codec_vector) {
    DCHECK_NE(codec, kExtendedVideoCodecStripped)
        << "codecs passed into this function should not be stripped";

    if (codec == kInvalidCodec)
      return false;

    if (use_aes_decryptor && codec == kUnsupportedByAesDecryptorCodec)
      return false;
  }

  return true;
}

// The IDL for MediaKeySystemConfiguration specifies some defaults, so
// create a config object that mimics what would be created if an empty
// dictionary was passed in.
WebMediaKeySystemConfiguration EmptyConfiguration() {
  // http://w3c.github.io/encrypted-media/#mediakeysystemconfiguration-dictionary
  // If this member (sessionTypes) is not present when the dictionary
  // is passed to requestMediaKeySystemAccess(), the dictionary will
  // be treated as if this member is set to [ "temporary" ].
  std::vector<WebEncryptedMediaSessionType> session_types;
  session_types.push_back(WebEncryptedMediaSessionType::kTemporary);

  WebMediaKeySystemConfiguration config;
  config.label = "";
  config.session_types = session_types;
  return config;
}

// EME spec requires that at least one of |video_capabilities| and
// |audio_capabilities| be specified. Add a single valid audio capability
// to the EmptyConfiguration().
WebMediaKeySystemConfiguration UsableConfiguration() {
  // Blink code parses the contentType into mimeType and codecs, so mimic
  // that here.
  std::vector<WebMediaKeySystemMediaCapability> audio_capabilities(1);
  audio_capabilities[0].mime_type = kSupportedAudioContainer;
  audio_capabilities[0].codecs = kSupportedAudioCodec;

  auto config = EmptyConfiguration();
  config.audio_capabilities = audio_capabilities;
  return config;
}

class FakeKeySystems : public KeySystems {
 public:
  ~FakeKeySystems() override = default;

  bool IsSupportedKeySystem(const std::string& key_system) const override {
    // Based on EME spec, Clear Key key system is always supported.
    return key_system == kSupportedKeySystem ||
           key_system == kClearKeyKeySystem;
  }

  bool CanUseAesDecryptor(const std::string& key_system) const override {
    return key_system == kClearKeyKeySystem;
  }

  // TODO(sandersd): Move implementation into KeySystemConfigSelector?
  bool IsSupportedInitDataType(const std::string& key_system,
                               EmeInitDataType init_data_type) const override {
    switch (init_data_type) {
      case EmeInitDataType::UNKNOWN:
        return false;
      case EmeInitDataType::WEBM:
        return init_data_type_webm_supported_;
      case EmeInitDataType::CENC:
        return init_data_type_cenc_supported_;
      case EmeInitDataType::KEYIDS:
        return init_data_type_keyids_supported_;
    }
    NOTREACHED();
    return false;
  }

  bool IsEncryptionSchemeSupported(
      const std::string& key_system,
      EncryptionMode encryption_scheme) const override {
    // TODO(crbug.com/658026): Implement this once value passed from blink.
    NOTREACHED();
    return false;
  }

  EmeConfigRule GetContentTypeConfigRule(
      const std::string& key_system,
      EmeMediaType media_type,
      const std::string& container_mime_type,
      const std::vector<std::string>& codecs) const override {
    DCHECK(IsValidContainerMimeType(container_mime_type))
        << "Invalid container mime type should not be passed in";
    if (container_mime_type == kUnsupportedContainer ||
        !IsCompatibleWithEmeMediaType(media_type, container_mime_type)) {
      return EmeConfigRule::NOT_SUPPORTED;
    }

    bool hw_secure_codec_required_ = false;
    bool hw_secure_codec_not_allowed_ = false;

    for (const std::string& codec : codecs) {
      DCHECK(IsValidCodec(codec)) << "Invalid codec should not be passed in";
      DCHECK_NE(codec, kExtendedVideoCodec)
          << "Extended codec should already been stripped";

      if (codec == kUnsupportedCodec ||
          !IsCompatibleWithEmeMediaType(media_type, codec)) {
        return EmeConfigRule::NOT_SUPPORTED;
      } else if (codec == kRequireHwSecureCodec) {
        hw_secure_codec_required_ = true;
      } else if (codec == kDisallowHwSecureCodec) {
        hw_secure_codec_not_allowed_ = true;
      }
    }

    if (hw_secure_codec_required_) {
      if (hw_secure_codec_not_allowed_)
        return EmeConfigRule::NOT_SUPPORTED;
      else
        return EmeConfigRule::HW_SECURE_CODECS_REQUIRED;
    }

    if (hw_secure_codec_not_allowed_)
      return EmeConfigRule::HW_SECURE_CODECS_NOT_ALLOWED;

    return EmeConfigRule::SUPPORTED;
  }

  EmeConfigRule GetRobustnessConfigRule(
      const std::string& key_system,
      EmeMediaType media_type,
      const std::string& requested_robustness) const override {
    if (requested_robustness.empty())
      return EmeConfigRule::SUPPORTED;
    if (requested_robustness == kSupportedRobustness)
      return EmeConfigRule::SUPPORTED;
    if (requested_robustness == kRequireIdentifierRobustness)
      return EmeConfigRule::IDENTIFIER_REQUIRED;
    if (requested_robustness == kRecommendIdentifierRobustness)
      return EmeConfigRule::IDENTIFIER_RECOMMENDED;
    if (requested_robustness == kDisallowHwSecureCodecRobustness)
      return EmeConfigRule::HW_SECURE_CODECS_NOT_ALLOWED;
    if (requested_robustness == kRequireHwSecureCodecRobustness)
      return EmeConfigRule::HW_SECURE_CODECS_REQUIRED;
    if (requested_robustness == kUnsupportedRobustness)
      return EmeConfigRule::NOT_SUPPORTED;

    NOTREACHED();
    return EmeConfigRule::NOT_SUPPORTED;
  }

  EmeSessionTypeSupport GetPersistentLicenseSessionSupport(
      const std::string& key_system) const override {
    return persistent_license;
  }

  EmeSessionTypeSupport GetPersistentReleaseMessageSessionSupport(
      const std::string& key_system) const override {
    return persistent_release_message;
  }

  EmeFeatureSupport GetPersistentStateSupport(
      const std::string& key_system) const override {
    return persistent_state;
  }

  EmeFeatureSupport GetDistinctiveIdentifierSupport(
      const std::string& key_system) const override {
    return distinctive_identifier;
  }

  bool init_data_type_webm_supported_ = false;
  bool init_data_type_cenc_supported_ = false;
  bool init_data_type_keyids_supported_ = false;

  // INVALID so that they must be set in any test that needs them.
  EmeSessionTypeSupport persistent_license = EmeSessionTypeSupport::INVALID;
  EmeSessionTypeSupport persistent_release_message =
      EmeSessionTypeSupport::INVALID;

  // Every test implicitly requires these, so they must be set. They are set to
  // values that are likely to cause tests to fail if they are accidentally
  // depended on. Test cases explicitly depending on them should set them, as
  // the default values may be changed.
  EmeFeatureSupport persistent_state = EmeFeatureSupport::NOT_SUPPORTED;
  EmeFeatureSupport distinctive_identifier = EmeFeatureSupport::REQUESTABLE;
};

class FakeMediaPermission : public MediaPermission {
 public:
  // MediaPermission implementation.
  void HasPermission(Type type,
                     const PermissionStatusCB& permission_status_cb) override {
    permission_status_cb.Run(is_granted);
  }

  void RequestPermission(
      Type type,
      const PermissionStatusCB& permission_status_cb) override {
    requests++;
    permission_status_cb.Run(is_granted);
  }

  bool IsEncryptedMediaEnabled() override { return is_encrypted_media_enabled; }

  int requests = 0;
  bool is_granted = false;
  bool is_encrypted_media_enabled = true;
};

}  // namespace

class KeySystemConfigSelectorTest : public testing::Test {
 public:
  KeySystemConfigSelectorTest()
      : key_systems_(new FakeKeySystems()),
        media_permission_(new FakeMediaPermission()) {}

  void SelectConfig() {
    media_permission_->requests = 0;
    succeeded_count_ = 0;
    not_supported_count_ = 0;
    KeySystemConfigSelector key_system_config_selector(key_systems_.get(),
                                                       media_permission_.get());

    key_system_config_selector.SetIsSupportedMediaTypeCBForTesting(
        base::BindRepeating(&IsSupportedMediaType));

    key_system_config_selector.SelectConfig(
        key_system_, configs_,
        base::BindRepeating(&KeySystemConfigSelectorTest::OnSucceeded,
                            base::Unretained(this)),
        base::BindRepeating(&KeySystemConfigSelectorTest::OnNotSupported,
                            base::Unretained(this)));
  }

  void SelectConfigReturnsConfig() {
    SelectConfig();
    EXPECT_EQ(0, media_permission_->requests);
    EXPECT_EQ(1, succeeded_count_);
    EXPECT_EQ(0, not_supported_count_);
    ASSERT_TRUE(succeeded_count_ != 0);
  }

  void SelectConfigReturnsError() {
    SelectConfig();
    EXPECT_EQ(0, media_permission_->requests);
    EXPECT_EQ(0, succeeded_count_);
    EXPECT_EQ(1, not_supported_count_);
    ASSERT_TRUE(not_supported_count_ != 0);
  }

  void SelectConfigRequestsPermissionAndReturnsConfig() {
    SelectConfig();
    EXPECT_EQ(1, media_permission_->requests);
    EXPECT_EQ(1, succeeded_count_);
    EXPECT_EQ(0, not_supported_count_);
    ASSERT_TRUE(media_permission_->requests != 0 && succeeded_count_ != 0);
  }

  void SelectConfigRequestsPermissionAndReturnsError() {
    SelectConfig();
    EXPECT_EQ(1, media_permission_->requests);
    EXPECT_EQ(0, succeeded_count_);
    EXPECT_EQ(1, not_supported_count_);
    ASSERT_TRUE(media_permission_->requests != 0 && not_supported_count_ != 0);
  }

  void OnSucceeded(const WebMediaKeySystemConfiguration& config,
                   const CdmConfig& cdm_config) {
    succeeded_count_++;
    config_ = config;
    cdm_config_ = cdm_config;
  }

  void OnNotSupported() { not_supported_count_++; }

  std::unique_ptr<FakeKeySystems> key_systems_;
  std::unique_ptr<FakeMediaPermission> media_permission_;

  // Held values for the call to SelectConfig().
  WebString key_system_ = WebString::FromUTF8(kSupportedKeySystem);
  std::vector<WebMediaKeySystemConfiguration> configs_;

  // Holds the selected configuration and CdmConfig.
  WebMediaKeySystemConfiguration config_;
  CdmConfig cdm_config_;

  int succeeded_count_;
  int not_supported_count_;

  DISALLOW_COPY_AND_ASSIGN(KeySystemConfigSelectorTest);
};

// --- Basics ---

TEST_F(KeySystemConfigSelectorTest, NoConfigs) {
  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, DefaultConfig) {
  auto config = EmptyConfiguration();

  // label = "";
  ASSERT_EQ("", config.label);

  // initDataTypes = [];
  ASSERT_EQ(0u, config.init_data_types.size());

  // audioCapabilities = [];
  ASSERT_EQ(0u, config.audio_capabilities.size());

  // videoCapabilities = [];
  ASSERT_EQ(0u, config.video_capabilities.size());

  // distinctiveIdentifier = "optional";
  ASSERT_EQ(MediaKeysRequirement::kOptional, config.distinctive_identifier);

  // persistentState = "optional";
  ASSERT_EQ(MediaKeysRequirement::kOptional, config.persistent_state);

  // If this member is not present when the dictionary is passed to
  // requestMediaKeySystemAccess(), the dictionary will be treated as
  // if this member is set to [ "temporary" ].
  ASSERT_EQ(1u, config.session_types.size());
  ASSERT_EQ(WebEncryptedMediaSessionType::kTemporary, config.session_types[0]);
}

TEST_F(KeySystemConfigSelectorTest, EmptyConfig) {
  // EME spec requires that at least one of |video_capabilities| and
  // |audio_capabilities| be specified.
  configs_.push_back(EmptyConfiguration());
  SelectConfigReturnsError();
}

// Most of the tests below assume that the the usable config is valid.
// Tests that touch |video_capabilities| and/or |audio_capabilities| can
// modify the empty config.

TEST_F(KeySystemConfigSelectorTest, UsableConfig) {
  configs_.push_back(UsableConfiguration());

  SelectConfigReturnsConfig();

  EXPECT_EQ("", config_.label);
  EXPECT_TRUE(config_.init_data_types.IsEmpty());
  EXPECT_EQ(1u, config_.audio_capabilities.size());
  EXPECT_TRUE(config_.video_capabilities.IsEmpty());
  EXPECT_EQ(MediaKeysRequirement::kNotAllowed, config_.distinctive_identifier);
  EXPECT_EQ(MediaKeysRequirement::kNotAllowed, config_.persistent_state);
  ASSERT_EQ(1u, config_.session_types.size());
  EXPECT_EQ(WebEncryptedMediaSessionType::kTemporary, config_.session_types[0]);

  EXPECT_FALSE(cdm_config_.allow_distinctive_identifier);
  EXPECT_FALSE(cdm_config_.allow_persistent_state);
  EXPECT_FALSE(cdm_config_.use_hw_secure_codecs);
}

TEST_F(KeySystemConfigSelectorTest, Label) {
  auto config = UsableConfiguration();
  config.label = "foo";
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_EQ("foo", config_.label);
}

// --- keySystem ---
// Empty is not tested because the empty check is in Blink.

TEST_F(KeySystemConfigSelectorTest, KeySystem_NonAscii) {
  key_system_ = "\xde\xad\xbe\xef";
  configs_.push_back(UsableConfiguration());
  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, KeySystem_Unsupported) {
  key_system_ = kUnsupportedKeySystem;
  configs_.push_back(UsableConfiguration());
  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, KeySystem_ClearKey) {
  key_system_ = kClearKeyKeySystem;
  configs_.push_back(UsableConfiguration());
  SelectConfigReturnsConfig();
}

// --- Disable EncryptedMedia ---

TEST_F(KeySystemConfigSelectorTest, EncryptedMediaDisabled_ClearKey) {
  media_permission_->is_encrypted_media_enabled = false;

  // Clear Key key system is always supported.
  key_system_ = kClearKeyKeySystem;
  configs_.push_back(UsableConfiguration());
  SelectConfigReturnsConfig();
}

TEST_F(KeySystemConfigSelectorTest, EncryptedMediaDisabled_Supported) {
  media_permission_->is_encrypted_media_enabled = false;

  // Other key systems are not supported.
  key_system_ = kSupportedKeySystem;
  configs_.push_back(UsableConfiguration());
  SelectConfigReturnsError();
}

// --- initDataTypes ---

TEST_F(KeySystemConfigSelectorTest, InitDataTypes_Empty) {
  auto config = UsableConfiguration();
  configs_.push_back(config);

  SelectConfigReturnsConfig();
}

TEST_F(KeySystemConfigSelectorTest, InitDataTypes_NoneSupported) {
  key_systems_->init_data_type_webm_supported_ = true;

  std::vector<WebEncryptedMediaInitDataType> init_data_types;
  init_data_types.push_back(WebEncryptedMediaInitDataType::kUnknown);
  init_data_types.push_back(WebEncryptedMediaInitDataType::kCenc);

  auto config = UsableConfiguration();
  config.init_data_types = init_data_types;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, InitDataTypes_SubsetSupported) {
  key_systems_->init_data_type_webm_supported_ = true;

  std::vector<WebEncryptedMediaInitDataType> init_data_types;
  init_data_types.push_back(WebEncryptedMediaInitDataType::kUnknown);
  init_data_types.push_back(WebEncryptedMediaInitDataType::kCenc);
  init_data_types.push_back(WebEncryptedMediaInitDataType::kWebm);

  auto config = UsableConfiguration();
  config.init_data_types = init_data_types;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.init_data_types.size());
  EXPECT_EQ(WebEncryptedMediaInitDataType::kWebm, config_.init_data_types[0]);
}

// --- distinctiveIdentifier ---

TEST_F(KeySystemConfigSelectorTest, DistinctiveIdentifier_Default) {
  key_systems_->distinctive_identifier = EmeFeatureSupport::REQUESTABLE;

  auto config = UsableConfiguration();
  config.distinctive_identifier = MediaKeysRequirement::kOptional;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kNotAllowed, config_.distinctive_identifier);
  EXPECT_FALSE(cdm_config_.allow_distinctive_identifier);
}

TEST_F(KeySystemConfigSelectorTest, DistinctiveIdentifier_Forced) {
  media_permission_->is_granted = true;
  key_systems_->distinctive_identifier = EmeFeatureSupport::ALWAYS_ENABLED;

  auto config = UsableConfiguration();
  config.distinctive_identifier = MediaKeysRequirement::kOptional;
  configs_.push_back(config);

  SelectConfigRequestsPermissionAndReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kRequired, config_.distinctive_identifier);
  EXPECT_TRUE(cdm_config_.allow_distinctive_identifier);
}

TEST_F(KeySystemConfigSelectorTest, DistinctiveIdentifier_Blocked) {
  key_systems_->distinctive_identifier = EmeFeatureSupport::NOT_SUPPORTED;

  auto config = UsableConfiguration();
  config.distinctive_identifier = MediaKeysRequirement::kRequired;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, DistinctiveIdentifier_RequestsPermission) {
  media_permission_->is_granted = true;
  key_systems_->distinctive_identifier = EmeFeatureSupport::REQUESTABLE;

  auto config = UsableConfiguration();
  config.distinctive_identifier = MediaKeysRequirement::kRequired;
  configs_.push_back(config);

  SelectConfigRequestsPermissionAndReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kRequired, config_.distinctive_identifier);
  EXPECT_TRUE(cdm_config_.allow_distinctive_identifier);
}

TEST_F(KeySystemConfigSelectorTest, DistinctiveIdentifier_RespectsPermission) {
  media_permission_->is_granted = false;
  key_systems_->distinctive_identifier = EmeFeatureSupport::REQUESTABLE;

  auto config = UsableConfiguration();
  config.distinctive_identifier = MediaKeysRequirement::kRequired;
  configs_.push_back(config);

  SelectConfigRequestsPermissionAndReturnsError();
}

// --- persistentState ---

TEST_F(KeySystemConfigSelectorTest, PersistentState_Default) {
  key_systems_->persistent_state = EmeFeatureSupport::REQUESTABLE;

  auto config = UsableConfiguration();
  config.persistent_state = MediaKeysRequirement::kOptional;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kNotAllowed, config_.persistent_state);
  EXPECT_FALSE(cdm_config_.allow_persistent_state);
}

TEST_F(KeySystemConfigSelectorTest, PersistentState_Forced) {
  key_systems_->persistent_state = EmeFeatureSupport::ALWAYS_ENABLED;

  auto config = UsableConfiguration();
  config.persistent_state = MediaKeysRequirement::kOptional;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kRequired, config_.persistent_state);
  EXPECT_TRUE(cdm_config_.allow_persistent_state);
}

TEST_F(KeySystemConfigSelectorTest, PersistentState_Blocked) {
  key_systems_->persistent_state = EmeFeatureSupport::ALWAYS_ENABLED;

  auto config = UsableConfiguration();
  config.persistent_state = MediaKeysRequirement::kNotAllowed;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

// --- sessionTypes ---

TEST_F(KeySystemConfigSelectorTest, SessionTypes_Empty) {
  auto config = UsableConfiguration();

  // Usable configuration has [ "temporary" ].
  std::vector<WebEncryptedMediaSessionType> session_types;
  config.session_types = session_types;

  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_TRUE(config_.session_types.IsEmpty());
}

TEST_F(KeySystemConfigSelectorTest, SessionTypes_SubsetSupported) {
  // Allow persistent state, as it would be required to be successful.
  key_systems_->persistent_state = EmeFeatureSupport::REQUESTABLE;
  key_systems_->persistent_license = EmeSessionTypeSupport::NOT_SUPPORTED;

  std::vector<WebEncryptedMediaSessionType> session_types;
  session_types.push_back(WebEncryptedMediaSessionType::kTemporary);
  session_types.push_back(WebEncryptedMediaSessionType::kPersistentLicense);

  auto config = UsableConfiguration();
  config.session_types = session_types;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, SessionTypes_AllSupported) {
  // Allow persistent state, and expect it to be required.
  key_systems_->persistent_state = EmeFeatureSupport::REQUESTABLE;
  key_systems_->persistent_license = EmeSessionTypeSupport::SUPPORTED;

  std::vector<WebEncryptedMediaSessionType> session_types;
  session_types.push_back(WebEncryptedMediaSessionType::kTemporary);
  session_types.push_back(WebEncryptedMediaSessionType::kPersistentLicense);

  auto config = UsableConfiguration();
  config.persistent_state = MediaKeysRequirement::kOptional;
  config.session_types = session_types;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kRequired, config_.persistent_state);
  ASSERT_EQ(2u, config_.session_types.size());
  EXPECT_EQ(WebEncryptedMediaSessionType::kTemporary, config_.session_types[0]);
  EXPECT_EQ(WebEncryptedMediaSessionType::kPersistentLicense,
            config_.session_types[1]);
}

TEST_F(KeySystemConfigSelectorTest, SessionTypes_PermissionCanBeRequired) {
  media_permission_->is_granted = true;
  key_systems_->distinctive_identifier = EmeFeatureSupport::REQUESTABLE;
  key_systems_->persistent_state = EmeFeatureSupport::REQUESTABLE;
  key_systems_->persistent_license =
      EmeSessionTypeSupport::SUPPORTED_WITH_IDENTIFIER;

  std::vector<WebEncryptedMediaSessionType> session_types;
  session_types.push_back(WebEncryptedMediaSessionType::kPersistentLicense);

  auto config = UsableConfiguration();
  config.distinctive_identifier = MediaKeysRequirement::kOptional;
  config.persistent_state = MediaKeysRequirement::kOptional;
  config.session_types = session_types;
  configs_.push_back(config);

  SelectConfigRequestsPermissionAndReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kRequired, config_.distinctive_identifier);
}

// --- videoCapabilities ---

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_Empty) {
  auto config = UsableConfiguration();
  configs_.push_back(config);

  SelectConfigReturnsConfig();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_ExtendedCodec) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kExtendedVideoCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_InvalidContainer) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kInvalidContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_UnsupportedContainer) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kUnsupportedContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_IncompatibleContainer) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedAudioContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_InvalidCodec) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kInvalidCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_UnsupportedCodec) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kInvalidContainer;
  video_capabilities[0].codecs = kUnsupportedCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_IncompatibleCodec) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedAudioCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest,
       VideoCapabilities_UnsupportedByAesDecryptorCodec_ClearKey) {
  key_system_ = kClearKeyKeySystem;

  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kUnsupportedByAesDecryptorCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest,
       VideoCapabilities_UnsupportedByAesDecryptorCodec) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kUnsupportedByAesDecryptorCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_SubsetSupported) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(2);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kInvalidContainer;
  video_capabilities[1].content_type = "b";
  video_capabilities[1].mime_type = kSupportedVideoContainer;
  video_capabilities[1].codecs = kSupportedVideoCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_EQ("b", config_.video_capabilities[0].content_type);
  EXPECT_EQ(kSupportedVideoContainer, config_.video_capabilities[0].mime_type);
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_AllSupported) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(2);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = GetSupportedVideoCodecs();
  video_capabilities[1].content_type = "b";
  video_capabilities[1].mime_type = kSupportedVideoContainer;
  video_capabilities[1].codecs = GetSupportedVideoCodecs();

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(2u, config_.video_capabilities.size());
  EXPECT_EQ("a", config_.video_capabilities[0].content_type);
  EXPECT_EQ("b", config_.video_capabilities[1].content_type);
}

// --- videoCapabilities Codecs ---

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_Codecs_SubsetInvalid) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = GetSubsetInvalidVideoCodecs();

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_Codecs_SubsetSupported) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = GetSubsetSupportedVideoCodecs();

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_Codecs_AllSupported) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = GetSupportedVideoCodecs();

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_EQ(GetSupportedVideoCodecs(), config_.video_capabilities[0].codecs);
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_Missing_Codecs) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

// --- videoCapabilities Robustness ---

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_Robustness_Empty) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;
  ASSERT_TRUE(video_capabilities[0].robustness.IsEmpty());

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_TRUE(config_.video_capabilities[0].robustness.IsEmpty());
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_Robustness_Supported) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;
  video_capabilities[0].robustness = kSupportedRobustness;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_EQ(kSupportedRobustness, config_.video_capabilities[0].robustness);
}

TEST_F(KeySystemConfigSelectorTest, VideoCapabilities_Robustness_Unsupported) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;
  video_capabilities[0].robustness = kUnsupportedRobustness;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest,
       VideoCapabilities_Robustness_PermissionCanBeRequired) {
  media_permission_->is_granted = true;
  key_systems_->distinctive_identifier = EmeFeatureSupport::REQUESTABLE;

  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;
  video_capabilities[0].robustness = kRequireIdentifierRobustness;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigRequestsPermissionAndReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kRequired, config_.distinctive_identifier);
}

TEST_F(KeySystemConfigSelectorTest,
       VideoCapabilities_Robustness_PermissionCanBeRecommended) {
  media_permission_->is_granted = false;
  key_systems_->distinctive_identifier = EmeFeatureSupport::REQUESTABLE;

  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;
  video_capabilities[0].robustness = kRecommendIdentifierRobustness;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigRequestsPermissionAndReturnsConfig();
  EXPECT_EQ(MediaKeysRequirement::kNotAllowed, config_.distinctive_identifier);
}

// --- HW Secure Codecs and Robustness ---

TEST_F(KeySystemConfigSelectorTest, HwSecureCodec_RequireHwSecureCodec) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kRequireHwSecureCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_TRUE(cdm_config_.use_hw_secure_codecs);
}

TEST_F(KeySystemConfigSelectorTest, HwSecureCodec_DisallowHwSecureCodec) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kDisallowHwSecureCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_FALSE(cdm_config_.use_hw_secure_codecs);
}

TEST_F(KeySystemConfigSelectorTest,
       HwSecureCodec_IncompatibleCodecAndRobustness) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kDisallowHwSecureCodec;
  video_capabilities[0].robustness = kRequireHwSecureCodecRobustness;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, HwSecureCodec_CompatibleCodecs) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs =
      MakeCodecs(kRequireHwSecureCodec, kSupportedVideoCodec);

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  EXPECT_TRUE(cdm_config_.use_hw_secure_codecs);
}

TEST_F(KeySystemConfigSelectorTest, HwSecureCodec_IncompatibleCodecs) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "a";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs =
      MakeCodecs(kRequireHwSecureCodec, kDisallowHwSecureCodec);

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest, HwSecureCodec_CompatibleCapabilityCodec) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(2);
  video_capabilities[0].content_type = "require_hw_secure_codec";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kRequireHwSecureCodec;
  video_capabilities[1].content_type = "supported_video_codec";
  video_capabilities[1].mime_type = kSupportedVideoContainer;
  video_capabilities[1].codecs = kSupportedVideoCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(2u, config_.video_capabilities.size());
  EXPECT_TRUE(cdm_config_.use_hw_secure_codecs);
}

TEST_F(KeySystemConfigSelectorTest, HwSecureCodec_RequireAndDisallow) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(2);
  video_capabilities[0].content_type = "require_hw_secure_codec";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kRequireHwSecureCodec;
  video_capabilities[1].content_type = "disallow_hw_secure_codec";
  video_capabilities[1].mime_type = kSupportedVideoContainer;
  video_capabilities[1].codecs = kDisallowHwSecureCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_EQ("require_hw_secure_codec",
            config_.video_capabilities[0].content_type);
  EXPECT_TRUE(cdm_config_.use_hw_secure_codecs);
}

TEST_F(KeySystemConfigSelectorTest, HwSecureCodec_DisallowAndRequire) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(2);
  video_capabilities[0].content_type = "disallow_hw_secure_codec";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kDisallowHwSecureCodec;
  video_capabilities[1].content_type = "require_hw_secure_codec";
  video_capabilities[1].mime_type = kSupportedVideoContainer;
  video_capabilities[1].codecs = kRequireHwSecureCodec;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_EQ("disallow_hw_secure_codec",
            config_.video_capabilities[0].content_type);
  EXPECT_FALSE(cdm_config_.use_hw_secure_codecs);
}

TEST_F(KeySystemConfigSelectorTest, HwSecureCodec_IncompatibleCapabilities) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(2);
  video_capabilities[0].content_type = "require_hw_secure_codec";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;
  video_capabilities[0].robustness = kRequireHwSecureCodecRobustness;
  video_capabilities[1].content_type = "disallow_hw_secure_codec";
  video_capabilities[1].mime_type = kSupportedVideoContainer;
  video_capabilities[1].codecs = kDisallowHwSecureCodec;
  video_capabilities[1].robustness = kUnsupportedRobustness;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_EQ("require_hw_secure_codec",
            config_.video_capabilities[0].content_type);
  EXPECT_TRUE(cdm_config_.use_hw_secure_codecs);
}

TEST_F(KeySystemConfigSelectorTest,
       HwSecureCodec_UnsupportedCapabilityNotAffectingRules) {
  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(2);
  video_capabilities[0].content_type = "unsupported_robustness";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kDisallowHwSecureCodec;
  video_capabilities[0].robustness = kUnsupportedRobustness;
  video_capabilities[1].content_type = "require_hw_secure_codec";
  video_capabilities[1].mime_type = kSupportedVideoContainer;
  video_capabilities[1].codecs = kRequireHwSecureCodec;
  video_capabilities[1].robustness = kRequireHwSecureCodecRobustness;

  auto config = EmptyConfiguration();
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_EQ("require_hw_secure_codec",
            config_.video_capabilities[0].content_type);
  EXPECT_TRUE(cdm_config_.use_hw_secure_codecs);
}

// --- audioCapabilities ---
// These are handled by the same code as |videoCapabilities|, so only minimal
// additional testing is done.

TEST_F(KeySystemConfigSelectorTest, AudioCapabilities_SubsetSupported) {
  std::vector<WebMediaKeySystemMediaCapability> audio_capabilities(2);
  audio_capabilities[0].content_type = "a";
  audio_capabilities[0].mime_type = kInvalidContainer;
  audio_capabilities[1].content_type = "b";
  audio_capabilities[1].mime_type = kSupportedAudioContainer;
  audio_capabilities[1].codecs = kSupportedAudioCodec;

  auto config = EmptyConfiguration();
  config.audio_capabilities = audio_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.audio_capabilities.size());
  EXPECT_EQ("b", config_.audio_capabilities[0].content_type);
  EXPECT_EQ(kSupportedAudioContainer, config_.audio_capabilities[0].mime_type);
}

// --- audioCapabilities and videoCapabilities ---

TEST_F(KeySystemConfigSelectorTest, AudioAndVideoCapabilities_AllSupported) {
  std::vector<WebMediaKeySystemMediaCapability> audio_capabilities(1);
  audio_capabilities[0].content_type = "a";
  audio_capabilities[0].mime_type = kSupportedAudioContainer;
  audio_capabilities[0].codecs = kSupportedAudioCodec;

  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "b";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;

  auto config = EmptyConfiguration();
  config.audio_capabilities = audio_capabilities;
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.audio_capabilities.size());
  ASSERT_EQ(1u, config_.video_capabilities.size());
}

TEST_F(KeySystemConfigSelectorTest,
       AudioAndVideoCapabilities_AudioUnsupported) {
  std::vector<WebMediaKeySystemMediaCapability> audio_capabilities(1);
  audio_capabilities[0].content_type = "a";
  audio_capabilities[0].mime_type = kUnsupportedContainer;
  audio_capabilities[0].codecs = kSupportedAudioCodec;

  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "b";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kSupportedVideoCodec;

  auto config = EmptyConfiguration();
  config.audio_capabilities = audio_capabilities;
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

TEST_F(KeySystemConfigSelectorTest,
       AudioAndVideoCapabilities_VideoUnsupported) {
  std::vector<WebMediaKeySystemMediaCapability> audio_capabilities(1);
  audio_capabilities[0].content_type = "a";
  audio_capabilities[0].mime_type = kSupportedAudioContainer;
  audio_capabilities[0].codecs = kSupportedAudioCodec;

  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(1);
  video_capabilities[0].content_type = "b";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kUnsupportedCodec;

  auto config = EmptyConfiguration();
  config.audio_capabilities = audio_capabilities;
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsError();
}

// Only "a2" and "v2" are supported types.
TEST_F(KeySystemConfigSelectorTest, AudioAndVideoCapabilities_SubsetSupported) {
  std::vector<WebMediaKeySystemMediaCapability> audio_capabilities(3);
  audio_capabilities[0].content_type = "a1";
  audio_capabilities[0].mime_type = kUnsupportedContainer;
  audio_capabilities[0].codecs = kSupportedAudioCodec;
  audio_capabilities[1].content_type = "a2";
  audio_capabilities[1].mime_type = kSupportedAudioContainer;
  audio_capabilities[1].codecs = kSupportedAudioCodec;
  audio_capabilities[2].content_type = "a3";
  audio_capabilities[2].mime_type = kSupportedAudioContainer;
  audio_capabilities[2].codecs = kUnsupportedCodec;

  std::vector<WebMediaKeySystemMediaCapability> video_capabilities(2);
  video_capabilities[0].content_type = "v1";
  video_capabilities[0].mime_type = kSupportedVideoContainer;
  video_capabilities[0].codecs = kUnsupportedCodec;
  video_capabilities[1].content_type = "v2";
  video_capabilities[1].mime_type = kSupportedVideoContainer;
  video_capabilities[1].codecs = kSupportedVideoCodec;

  auto config = EmptyConfiguration();
  config.audio_capabilities = audio_capabilities;
  config.video_capabilities = video_capabilities;
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ(1u, config_.audio_capabilities.size());
  EXPECT_EQ("a2", config_.audio_capabilities[0].content_type);
  ASSERT_EQ(1u, config_.video_capabilities.size());
  EXPECT_EQ("v2", config_.video_capabilities[0].content_type);
}

// --- Multiple configurations ---

TEST_F(KeySystemConfigSelectorTest, Configurations_AllSupported) {
  auto config = UsableConfiguration();
  config.label = "a";
  configs_.push_back(config);
  config.label = "b";
  configs_.push_back(config);

  SelectConfigReturnsConfig();
  ASSERT_EQ("a", config_.label);
}

TEST_F(KeySystemConfigSelectorTest, Configurations_SubsetSupported) {
  auto config1 = UsableConfiguration();
  config1.label = "a";
  std::vector<WebEncryptedMediaInitDataType> init_data_types;
  init_data_types.push_back(WebEncryptedMediaInitDataType::kUnknown);
  config1.init_data_types = init_data_types;
  configs_.push_back(config1);

  auto config2 = UsableConfiguration();
  config2.label = "b";
  configs_.push_back(config2);

  SelectConfigReturnsConfig();
  ASSERT_EQ("b", config_.label);
}

TEST_F(KeySystemConfigSelectorTest,
       Configurations_FirstRequiresPermission_Allowed) {
  media_permission_->is_granted = true;
  key_systems_->distinctive_identifier = EmeFeatureSupport::REQUESTABLE;

  auto config1 = UsableConfiguration();
  config1.label = "a";
  config1.distinctive_identifier = MediaKeysRequirement::kRequired;
  configs_.push_back(config1);

  auto config2 = UsableConfiguration();
  config2.label = "b";
  configs_.push_back(config2);

  SelectConfigRequestsPermissionAndReturnsConfig();
  ASSERT_EQ("a", config_.label);
}

TEST_F(KeySystemConfigSelectorTest,
       Configurations_FirstRequiresPermission_Rejected) {
  media_permission_->is_granted = false;
  key_systems_->distinctive_identifier = EmeFeatureSupport::REQUESTABLE;

  auto config1 = UsableConfiguration();
  config1.label = "a";
  config1.distinctive_identifier = MediaKeysRequirement::kRequired;
  configs_.push_back(config1);

  auto config2 = UsableConfiguration();
  config2.label = "b";
  configs_.push_back(config2);

  SelectConfigRequestsPermissionAndReturnsConfig();
  ASSERT_EQ("b", config_.label);
}

}  // namespace media
