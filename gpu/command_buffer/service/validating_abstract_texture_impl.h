// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_VALIDATING_ABSTRACT_TEXTURE_IMPL_H_
#define GPU_COMMAND_BUFFER_SERVICE_VALIDATING_ABSTRACT_TEXTURE_IMPL_H_

#include "gpu/command_buffer/service/abstract_texture.h"

#include "base/callback.h"
#include "base/memory/scoped_refptr.h"

namespace gpu {
class DecoderContext;

namespace gles2 {

class ContextGroup;
class ErrorState;
class GLStreamTextureImage;
class TextureManager;
class TextureRef;

// Implementation of AbstractTexture used by the validating command decoder.
class ValidatingAbstractTextureImpl : public AbstractTexture {
 public:
  using DestructionCB = base::OnceCallback<void(ValidatingAbstractTextureImpl*,
                                                scoped_refptr<TextureRef>)>;

  ValidatingAbstractTextureImpl(scoped_refptr<TextureRef> texture_ref,
                                DecoderContext* DecoderContext,
                                DestructionCB destruction_cb);
  ~ValidatingAbstractTextureImpl() override;

  // AbstractTexture
  TextureBase* GetTextureBase() const override;
  void SetParameteri(GLenum pname, GLint param) override;
  void SetOverlayImage(gl::GLImage* image) override;
  void SetStreamTextureImage(GLStreamTextureImage* image,
                             GLuint service_id) override;

  // Called when our decoder is going away, so that we can try to clean up.
  void OnDecoderWillDestroy(bool have_context);

 private:
  TextureManager* GetTextureManager() const;
  ContextGroup* GetContextGroup() const;
  ErrorState* GetErrorState() const;

  scoped_refptr<TextureRef> texture_ref_;
  DecoderContext* decoder_context_ = nullptr;
  DestructionCB destruction_cb_;
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_VALIDATING_ABSTRACT_TEXTURE_IMPL_H_
