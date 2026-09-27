<!-- Issue title: [Windows] FlutterEngine::PostPlatformThreadTask crashes: on_cancel is called immediately, freeing the task before it runs -->

### Steps to reproduce

1. Clone https://github.com/Gustl22/flutter_issue_post_platform_thread_app
2. `flutter run -d windows`

The app posts a task via `flutter::FlutterEngine::PostPlatformThreadTask` on startup
(and again on each press of the "Post task" button). All changes compared to a plain
`flutter create --platforms=windows` app are in commit
[f206d91](https://github.com/Gustl22/flutter_issue_post_platform_thread_app/commit/f206d915593782d014b5bb6e938dee45d144c51b).

### Expected results

The posted task runs on the platform thread and replies to Dart. `on_cancel` of
`FlutterDesktopEnginePostPlatformThreadTask` is only called if the task is discarded
without running, as documented in `flutter_windows.h`:

> If the task is discarded without being executed (e.g. during engine
> shutdown), |on_cancel| is called on the platform thread so the caller can
> cleanup allocations.

### Actual results

The app crashes with an access violation (`0xC0000005`) as soon as the task runs.

`FlutterDesktopEnginePostPlatformThreadTask` calls `on_cancel` **synchronously while
posting**, and then still runs `callback` later on the platform thread. So every task
gets both callbacks. `FlutterEngine::PostPlatformThreadTask` frees the heap-allocated
`std::function` in `on_cancel`, so the task is destroyed before it runs (see
`task destroyed` before `task posted` in the logs), and the platform thread then invokes
the freed `std::function` (use-after-free).

#### Cause

In `engine/src/flutter/shell/platform/windows/flutter_windows.cc`:

```cpp
auto context =
    std::make_shared<Context>(Context{callback, on_cancel, user_data});
```

`Context{...}` creates a temporary, which `std::make_shared` copies (`Context` has a
user-declared destructor, so it has no implicit move constructor). When the temporary
is destroyed at the end of the statement, `~Context()` sees `invoked == false` and calls
`on_cancel`.

Introduced in #187365.

### Code sample

<details open><summary>Code sample</summary>

Full app: https://github.com/Gustl22/flutter_issue_post_platform_thread_app
(relevant changes: [f206d91](https://github.com/Gustl22/flutter_issue_post_platform_thread_app/commit/f206d915593782d014b5bb6e938dee45d144c51b))

`windows/runner/flutter_window.cpp`:

```cpp
// Captured by the posted task; logs when the task (and thus its captures) is
// destroyed, which should only happen after the task has run.
struct DestructionLogger {
  ~DestructionLogger() {
    std::cout << "[runner] task destroyed" << std::endl;
  }
};

// In a method call handler for "postTask":
std::cout << "[runner] posting task" << std::endl;
engine->PostPlatformThreadTask(
    [shared_result, logger = std::make_shared<DestructionLogger>()]() {
      std::cout << "[runner] task running" << std::endl;
      shared_result->Success(flutter::EncodableValue(
          "Task ran on the platform thread"));
    });
std::cout << "[runner] task posted" << std::endl;
```

</details>

### Screenshots or Video

_No response_

### Logs

<details open><summary>Logs</summary>

Debug build with Flutter 3.47.5 (stable):

```console
The Dart VM service is listening on http://127.0.0.1:54107/T8Rqs4UkRRg=/
[runner] posting task
[runner] task destroyed
[runner] task posted
Warning: Failed to respond to a message. This is a memory leak.
```

The process then exits with `0xC0000005`. Windows event log:

```console
Faulting application name: post_platform_thread_app.exe, version: 1.0.0.1
Faulting module name: post_platform_thread_app.exe, version: 1.0.0.1
Exception code: 0xc0000005
Fault offset: 0x000000000004ac78
```

Expected output (verified with a locally built engine that includes the fix):

```console
[runner] posting task
[runner] task posted
[runner] task running
[runner] task destroyed
```

</details>

### Flutter Doctor output

<details open><summary>Doctor output</summary>

```console
[√] Flutter (Channel stable, 3.47.5, on Microsoft Windows [Version 10.0.28000.2956], locale en-DE) [345ms]
    • Flutter version 3.47.5 on channel stable at C:\tools\flutter
    • Upstream repository https://github.com/flutter/flutter.git
    • Framework revision 6a19cca564 (10 days ago), 2026-09-17 14:13:22 -0400
    • Engine revision af7e796e16
    • Dart version 3.13.4
    • DevTools version 2.60.0
    • Feature flags: enable-web, enable-linux-desktop, enable-macos-desktop, enable-windows-desktop, enable-android, enable-ios, cli-animations, enable-native-assets, enable-record-use, enable-swift-package-manager, omit-legacy-version-file, enable-lldb-debugging, enable-uiscene-migration

[√] Windows Version (Windows 11 or higher, 26H1, 2009) [680ms]

[X] Android toolchain - develop for Android devices [185ms]
    X Unable to locate Android SDK.

[√] Chrome - develop for the web [144ms]
    • Chrome at C:\Program Files\Google\Chrome\Application\chrome.exe

[√] Visual Studio - develop Windows apps (Visual Studio Build Tools 2026 18.10.2) [143ms]
    • Visual Studio at C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools
    • Visual Studio Build Tools 2026 version 18.10.12217.157
    • Windows 10 SDK version 10.0.26100.0

[√] Connected device (3 available) [117ms]
    • Windows (desktop) • windows • windows-arm64  • Microsoft Windows [Version 10.0.28000.2956]
    • Chrome (web)      • chrome  • web-javascript • Google Chrome 153.0.8010.53
    • Edge (web)        • edge    • web-javascript • Microsoft Edge 153.0.4234.48

[√] Network resources [649ms]
    • All expected network resources are available.

! Doctor found issues in 1 category.
```

</details>
