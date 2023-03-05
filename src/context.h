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

#pragma once

#include "../include/comp_surf_impeller/comp_surf_impeller.h"
#include "examples/example_base.h"
#include "impeller/renderer/backend/gles/context_gles.h"
#include "impeller/renderer/renderer.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <memory>
#include <string>

struct CompSurfContext {
 public:
  static uint32_t version();

  CompSurfContext(const char* accessToken,
                  int width,
                  int height,
                  void* nativeWindow,
                  const char* assetsPath,
                  const char* cachePath,
                  const char* miscPath);

  ~CompSurfContext() = default;

  CompSurfContext(const CompSurfContext&) = delete;

  CompSurfContext(CompSurfContext&&) = delete;

  CompSurfContext& operator=(const CompSurfContext&) = delete;

  void de_initialize() const;

  void run_task();

  void draw_frame(uint32_t time) const;

  void resize(int width, int height);

 private:
  std::string mAccessToken;
  std::string mAssetsPath;
  std::string mCachePath;
  std::string mMiscPath;

  int mWidth;
  int mHeight;

  std::shared_ptr<impeller::ContextGLES> mContext;
  std::unique_ptr<impeller::Renderer> mRenderer;
  std::vector<std::unique_ptr<example::ExampleBase>> mExamples;
  std::vector<const char*> mExampleNames;

  struct NATIVE_WINDOW {
    struct wl_display* wl_display;
    struct wl_surface* wl_surface;
    EGLDisplay egl_display;
    struct wl_egl_window* egl_window;
    uint32_t width;
    uint32_t height;
  };

  NATIVE_WINDOW* mNativeWindow;

  struct {
    EGLSurface eglSurface;
    EGLContext eglContext;
  } mEgl{};

  void init_egl(void* nativeWindow,
                EGLDisplay& eglDisplay,
                EGLSurface& eglSurface,
                EGLContext& eglContext);
  void* get_egl_proc_address(const char* address);
  EGLSurface create_egl_surface(EGLDisplay& eglDisplay,
                                EGLConfig& eglConfig,
                                void* native_window,
                                const EGLint* attrib_list);
};
