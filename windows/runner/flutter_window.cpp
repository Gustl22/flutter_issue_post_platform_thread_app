#include "flutter_window.h"

#include <flutter/standard_method_codec.h>

#include <iostream>
#include <memory>
#include <optional>

#include "flutter/generated_plugin_registrant.h"

namespace {

// Captured by the posted task; logs when the task (and thus its captures) is
// destroyed, which should only happen after the task has run.
struct DestructionLogger {
  ~DestructionLogger() {
    std::cout << "[runner] task destroyed" << std::endl;
  }
};

}  // namespace

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());

  channel_ = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      flutter_controller_->engine()->messenger(), "post_platform_thread",
      &flutter::StandardMethodCodec::GetInstance());
  channel_->SetMethodCallHandler(
      [engine = flutter_controller_->engine()](
          const flutter::MethodCall<flutter::EncodableValue>& call,
          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
              result) {
        if (call.method_name() != "postTask") {
          result->NotImplemented();
          return;
        }
        std::shared_ptr<flutter::MethodResult<flutter::EncodableValue>>
            shared_result = std::move(result);

        std::cout << "[runner] posting task" << std::endl;
        engine->PostPlatformThreadTask(
            [shared_result, logger = std::make_shared<DestructionLogger>()]() {
              std::cout << "[runner] task running" << std::endl;
              shared_result->Success(flutter::EncodableValue(
                  "Task ran on the platform thread"));
            });
        std::cout << "[runner] task posted" << std::endl;
      });

  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  channel_ = nullptr;
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
