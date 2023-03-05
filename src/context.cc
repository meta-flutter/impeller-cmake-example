/*
 * Copyright 2022 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

#include "fml/mapping.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/imgui.h"
#include "impeller/playground/imgui/gles/imgui_shaders_gles.h"
#include "impeller/playground/imgui/imgui_impl_impeller.h"
#include "impeller/renderer/allocator.h"
#include "impeller/renderer/backend/gles/context_gles.h"
#include "impeller/renderer/backend/gles/reactor_gles.h"
#include "impeller/renderer/backend/gles/surface_gles.h"
#include "impeller/renderer/formats.h"
#include "impeller/renderer/renderer.h"
#include "impeller/renderer/texture_descriptor.h"

#include "examples/example_base.h"
#include "examples/mesh/mesh_example.h"
#include "examples/the_impeller/the_impeller_example.h"

#include "generated/shaders/gles/example_shaders_gles.h"

#include "context.h"

class ReactorWorker final : public impeller::ReactorGLES::Worker {
 public:
  ReactorWorker() = default;

  // |ReactorGLES::Worker|
  bool CanReactorReactOnCurrentThreadNow(
      const impeller::ReactorGLES& reactor) const override {
    impeller::ReaderLock lock(mutex_);
    auto found = reactions_allowed_.find(std::this_thread::get_id());
    if (found == reactions_allowed_.end()) {
      return false;
    }
    return found->second;
  }

  void SetReactionsAllowedOnCurrentThread(bool allowed) {
    impeller::WriterLock lock(mutex_);
    reactions_allowed_[std::this_thread::get_id()] = allowed;
  }

 private:
  mutable impeller::RWMutex mutex_;
  std::map<std::thread::id, bool> reactions_allowed_ IPLR_GUARDED_BY(mutex_);

  FML_DISALLOW_COPY_AND_ASSIGN(ReactorWorker);
};

CompSurfContext::CompSurfContext(const char* accessToken,
                                 int width,
                                 int height,
                                 void* nativeWindow,
                                 const char* assetsPath,
                                 const char* cachePath,
                                 const char* miscPath)
    : mAccessToken(accessToken),
      mAssetsPath(assetsPath),
      mCachePath(cachePath),
      mMiscPath(miscPath),
      mNativeWindow(reinterpret_cast<NATIVE_WINDOW*>(nativeWindow)) {
  std::cout << "[comp_surf_cxx]" << std::endl;
  std::cout << "assetsPath: " << mAssetsPath << std::endl;
  std::cout << "cachePath: " << mCachePath << std::endl;
  std::cout << "miscPath: " << mMiscPath << std::endl;

  assert(mNativeWindow);
  assert(mNativeWindow->egl_display);
  assert(mNativeWindow->egl_window);

  init_egl(mNativeWindow->egl_window, mNativeWindow->egl_display,
           mEgl.eglSurface, mEgl.eglContext);

  auto ret = eglMakeCurrent(mNativeWindow->egl_display, mEgl.eglSurface,
                            mEgl.eglSurface, mEgl.eglContext);
  assert(ret == EGL_TRUE);

  //----------------------------------------------------------------------------
  /// Create an Impeller context.
  ///

  auto resolver = [this](const char* name) -> void* {
    return reinterpret_cast<void*>(::eglGetProcAddress(name));
  };
  auto gl = std::make_unique<impeller::ProcTableGLES>(resolver);
  if (!gl->IsValid()) {
    std::cerr << "Failed to create a valid GLES proc table.";
    assert(false);
  }

  mContext = impeller::ContextGLES::Create(
      std::move(gl), {std::make_shared<fml::NonOwnedMapping>(
                          impeller_imgui_shaders_gles_data,
                          impeller_imgui_shaders_gles_length),
                      std::make_shared<fml::NonOwnedMapping>(
                          impeller_example_shaders_gles_data,
                          impeller_example_shaders_gles_length)});
  if (!mContext) {
    std::cerr << "Failed to create Impeller context.";
    assert(false);
  }

  auto worker = std::make_shared<ReactorWorker>();
  worker->SetReactionsAllowedOnCurrentThread(true);
  auto worker_id = mContext->AddReactorWorker(worker);
  if (!worker_id.has_value()) {
    std::cerr << "Failed to register GLES reactor worker.";
    assert(false);
  }

  mRenderer = std::make_unique<impeller::Renderer>(mContext);

  //----------------------------------------------------------------------------

  /// Setup examples.
  ///

  mExamples.push_back(std::make_unique<example::TheImpellerExample>());
//  mExamples.push_back(std::make_unique<example::MeshExample>());

  for (auto& item : mExamples) {
    auto info = item->GetInfo();
    mExampleNames.push_back(info.name);

    if (!item->Setup(*mRenderer->GetContext())) {
      return;
    }
  }

  eglMakeCurrent(mNativeWindow->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 EGL_NO_CONTEXT);
}

void* CompSurfContext::get_egl_proc_address(const char* address) {
  const char* extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

  if (extensions && (strstr(extensions, "EGL_EXT_platform_wayland") ||
                     strstr(extensions, "EGL_KHR_platform_wayland"))) {
    return (void*)eglGetProcAddress(address);
  }

  return nullptr;
}

EGLSurface CompSurfContext::create_egl_surface(EGLDisplay& eglDisplay,
                                               EGLConfig& eglConfig,
                                               void* native_window,
                                               const EGLint* attrib_list) {
  static PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_platform_window =
      nullptr;

  if (!create_platform_window) {
    create_platform_window =
        (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)get_egl_proc_address(
            "eglCreatePlatformWindowSurfaceEXT");
  }

  if (create_platform_window)
    return create_platform_window(eglDisplay, eglConfig, native_window,
                                  attrib_list);

  return eglCreateWindowSurface(
      eglDisplay, eglConfig, (EGLNativeWindowType)native_window, attrib_list);
}

void CompSurfContext::init_egl(void* nativeWindow,
                               EGLDisplay& eglDisplay,
                               EGLSurface& eglSurface,
                               EGLContext& eglContext) {
  constexpr int kEglBufferSize = 24;

  EGLint config_attribs[] = {
      // clang-format off
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_STENCIL_SIZE, 8, // 8 bit stencil buffer
            EGL_SAMPLES, 4,      // 4xMSAA
            EGL_NONE
      // clang-format on
  };

  static const EGLint context_attribs[] = {
      // clang-format off
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MAJOR_VERSION, 2,
            EGL_NONE
      // clang-format on
  };

  EGLint major, minor;
  EGLBoolean ret = eglInitialize(eglDisplay, &major, &minor);
  assert(ret == EGL_TRUE);

  ret = eglBindAPI(EGL_OPENGL_ES_API);
  assert(ret == EGL_TRUE);

  EGLint count;
  eglGetConfigs(eglDisplay, nullptr, 0, &count);
  assert(count);

  auto* configs =
      reinterpret_cast<EGLConfig*>(calloc(count, sizeof(EGLConfig)));
  assert(configs);

  EGLint n;
  ret = eglChooseConfig(eglDisplay, config_attribs, configs, count, &n);
  assert(ret && n >= 1);

  EGLint size;
  EGLConfig egl_conf = nullptr;
  for (EGLint i = 0; i < n; i++) {
    eglGetConfigAttrib(eglDisplay, configs[i], EGL_BUFFER_SIZE, &size);
    if (kEglBufferSize <= size) {
      egl_conf = configs[i];
      break;
    }
  }
  free(configs);
  if (egl_conf == nullptr) {
    assert(false);
  }

  eglContext =
      eglCreateContext(eglDisplay, egl_conf, EGL_NO_CONTEXT, context_attribs);
  eglSurface = create_egl_surface(eglDisplay, egl_conf, nativeWindow, nullptr);
  assert(eglSurface != EGL_NO_SURFACE);
}

void CompSurfContext::de_initialize() const {}

void CompSurfContext::run_task() {}

void CompSurfContext::resize(int width, int height) {}

void CompSurfContext::draw_frame(uint32_t time) const {
  eglMakeCurrent(mNativeWindow->egl_display, mEgl.eglSurface, mEgl.eglSurface,
                 mEgl.eglContext);

#if 0 //TODO
  ::ImGui_ImplGlfw_NewFrame();
#endif

  /// Get the next surface.

  impeller::SurfaceGLES::SwapCallback swap_callback = [this]() -> bool {
    eglSwapBuffers(mNativeWindow->egl_display, mEgl.eglSurface);
    return true;
  };

  auto surface = impeller::SurfaceGLES::WrapFBO(
      mContext, swap_callback, 0u, impeller::PixelFormat::kR8G8B8A8UNormInt,
#if 0
      kA8UNormInt,
      kR8G8B8A8UNormInt,
      kR8G8B8A8UNormIntSRGB,
      kB8G8R8A8UNormInt,
      kB8G8R8A8UNormIntSRGB,
      kS8UInt,
#endif
      impeller::ISize::MakeWH(mWidth, mHeight));

  /// Render to the surface.
#if 0 //TODO
  ImGui::SetNextWindowPos({10, 10});
#endif

  impeller::Renderer::RenderCallback render_callback =
      [this](impeller::RenderTarget& render_target) -> bool {
    static int selected_example_index = 1;
    auto example = mExamples[selected_example_index].get();
    auto example_info = example->GetInfo();

#if 0 //TODO
    ImGui::NewFrame();
    ImGui::Begin(example_info.name, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      if (ImGui::SmallButton("<")) {
        selected_example_index -= 1;
      }
      ImGui::SameLine(200);
      if (ImGui::SmallButton(">")) {
        selected_example_index += 1;
      }
      while (selected_example_index < 0) {
        selected_example_index += mExampleNames.size();
      }
      while (selected_example_index >= mExampleNames.size()) {
        selected_example_index -= mExampleNames.size();
      }
      ImGui::ListBox("##", &selected_example_index, mExampleNames.data(),
                     mExampleNames.size());
      ImGui::TextWrapped("%s", example_info.description);
    }
#endif //TODO

    auto buffer = mRenderer->GetContext()->CreateCommandBuffer();
    if (!buffer) {
      return false;
    }
    buffer->SetLabel("Command Buffer");

    /// Setup depth attachment.

    {
      impeller::TextureDescriptor depth_texture_desc;
      depth_texture_desc.type = impeller::TextureType::kTexture2D;
      depth_texture_desc.format = impeller::PixelFormat::kR8G8B8A8UNormInt;
      depth_texture_desc.size = render_target.GetRenderTargetSize();
      depth_texture_desc.usage = static_cast<impeller::TextureUsageMask>(
          impeller::TextureUsage::kRenderTarget);
      depth_texture_desc.sample_count = impeller::SampleCount::kCount1;
      depth_texture_desc.storage_mode = impeller::StorageMode::kDevicePrivate;

      impeller::DepthAttachment depth;
      depth.load_action = impeller::LoadAction::kClear;
      depth.store_action = impeller::StoreAction::kDontCare;
      depth.clear_depth = 1.0;
      depth.texture =
          mRenderer->GetContext()->GetResourceAllocator()->CreateTexture(
              depth_texture_desc);

      render_target.SetDepthAttachment(depth);
    }

    // Render the selected example.
    if (!example->Render(*mRenderer->GetContext(), render_target, *buffer)) {
      return false;
    }

#if 0 //TODO
    ImGui::End();
    ImGui::Render();

    // Render ImGui overlay.
    {
      if (render_target.GetColorAttachments().empty()) {
        return false;
      }
      auto color0 = render_target.GetColorAttachments().find(0)->second;
      color0.load_action = impeller::LoadAction::kLoad;
      render_target.SetColorAttachment(color0, 0);
      auto pass = buffer->CreateRenderPass(render_target);
      if (!pass) {
        return false;
      }
      pass->SetLabel("ImGui Render Pass");

      ::ImGui_ImplImpeller_RenderDrawData(ImGui::GetDrawData(), *pass);

      if (!pass->EncodeCommands()) {
        return false;
      }
    }
#endif //TODO

    return buffer->SubmitCommands();
  };
  mRenderer->Render(std::move(surface), render_callback);

  eglMakeCurrent(mNativeWindow->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 EGL_NO_CONTEXT);
}
