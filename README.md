> ⚠️ **WIP — pre-release.** The hosting pattern inside is production-proven (it is how a shipping react-native-windows 0.83 new-architecture app renders WebView2 today), but this standalone header has NOT yet been compile-validated as packaged here. Treat it as reviewed reference code until the first tagged release.

# rnw-native-core

**The missing primitive for react-native-windows New Architecture:** host
*any* WinUI 3 / XAML control inside a react-native-windows (Fabric/Composition)
app — sized, clipped, scrolled, DPI-correct, focusable, and accessible — with
one include and ~60 lines of subclass code.

```
your WinUI control (any Microsoft.UI.Xaml.UIElement)
  └─ Microsoft.UI.Xaml.XamlIsland            (pure island, no HWND site)
       └─ island.ContentIsland()
            ──Connect()──▶ RNW ContentIslandComponentView
                           (ChildSiteLink: size / position / scroll-transform /
                            clipping / DPI / focus / UIA — RNW maintains all of it)
```

## Why this repo exists

RNW's New Architecture shipped a first-class foreign-content host —
`ContentIslandComponentView` — but nobody packaged the pattern. Every library
author who wants to put a real Windows control (WebView2, MediaPlayerElement,
a camera preview surface, a map control, a PDF viewer…) inside the RN tree has
to rediscover the same five-step dance:

1. `IReactPackageBuilderFabric.AddViewComponent(name, provider)` — there is
   **no attributed auto-registration** for view components;
2. `builder.XamlSupport(true)` so RNW bootstraps the WinUI XAML framework;
3. `IReactCompositionViewComponentBuilder.SetContentIslandComponentViewInitializer`
   to receive the per-mount `ContentIslandComponentView`;
4. `XamlIsland` → `Content(control)` → `view.Connect(island.ContentIsland())`;
5. a Tag-keyed instance registry with **Destroying-hooked teardown** (leaked
   islands break input routing window-wide — we learned this the hard way).

This repo is that dance as a one-include, header-only base class
(`include/rnwcore/XamlIslandComponent.h`) plus a worked example
(`example/BadgeView.{h,cpp}`) and the full receipts
(`docs/VERIFIED-API.md` — exact RNW 0.83.2 file:line citations for every ABI
call).

## This pattern ships today

- The pattern was extracted from the **WebView2 Fabric component running in a
  production app** (Facilitron FIT for Windows: Expo + react-native-windows
  0.83.2 new-arch, WinAppSDK 1.8, ARM64) — real web content rendering inside
  the RN composition tree, clipped and scrolled correctly, since July 2026.
  The productized form of that component lives at
  [FacilitronWorks/react-native-webview-windows](https://github.com/FacilitronWorks/react-native-webview-windows).
- **Independent validation:** upstream `react-native-webview@15.0.0`
  (July 2026, [PR #3973](https://github.com/react-native-webview/react-native-webview/pull/3973))
  shipped its own in-tree Windows New-Architecture implementation built on the
  **same XamlIsland/ContentIsland architecture**. Two teams converged on this
  design without coordinating; this repo makes it a primitive so the third
  team doesn't have to.

## Usage

```cpp
// MyVideoView.h
#include <rnwcore/XamlIslandComponent.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

class MyVideoView final : public rnwcore::XamlIslandComponent {
 public:
  winrt::Microsoft::UI::Xaml::UIElement CreateContent() override {
    m_player = winrt::Microsoft::UI::Xaml::Controls::MediaPlayerElement{};
    return m_player;
  }
  void OnPropsUpdated(winrt::Microsoft::ReactNative::IComponentProps const& newProps,
                      winrt::Microsoft::ReactNative::IComponentProps const&) noexcept override { /* … */ }
  void OnCommand(winrt::hstring const& name,
                 winrt::Microsoft::ReactNative::IJSValueReader const& args) noexcept override { /* … */ }
  void OnDestroying() noexcept override { m_player = nullptr; }
 private:
  winrt::Microsoft::UI::Xaml::Controls::MediaPlayerElement m_player{nullptr};
};

// In your IReactPackageProvider::CreatePackage:
rnwcore::XamlIslandComponentRegistrar<MyVideoView>::Register(
    packageBuilder, L"MyVideoView");
```

The base class + registrar handle registration, XAML bootstrap, island
connect, props/commands/event-emitter plumbing, the Tag-keyed registry, and
Destroying-hooked teardown (`OnDestroying()` → `XamlIsland.Close()` → registry
erase). Events go out via `Emitter().DispatchEvent(L"topMyEvent", …)` —
`top*`-prefixed names pass through RNW's event pipeline verbatim (citation in
`docs/VERIFIED-API.md`).

## Status matrix

| Piece | Status |
| --- | --- |
| Hosting pattern (island bootstrap, Connect, registry, teardown) | ✅ **Production-proven** inside a shipping app's WebView component |
| ABI surface used | ✅ Verified against RNW 0.83.2 IDLs/sources, file:line cited in `docs/VERIFIED-API.md` |
| This header as packaged (generalized base class + registrar template) | ⏳ **Pending** — extraction from a working production app; standalone build validation pending |
| `example/BadgeView` | ⏳ Compiling-quality, never compiled standalone |
| Old-architecture hosts | ❌ Not supported (Composition/Fabric only) — guard registration with `RNW_NEW_ARCH` |
| NuGet / npm distribution | ❌ Not yet — consume by vendoring `include/rnwcore/` (header-only) |
| Async-subclass helper (dispatcher re-assert awaiter) | ❌ Planned; pattern documented in the header comments |

## Hard-won caveats baked into the design

- **Teardown is not optional.** Erase the registry entry and `Close()` the
  island on the view's `Destroying` event. Leaked islands are known to break
  input routing for the whole window.
- **Everything is UI-thread.** Fabric callbacks arrive on the RNW UI thread and
  XAML islands are thread-affine to it. Subclasses doing `co_await` must
  re-assert that thread after each hop.
- **Don't alias `comp`.** RNW's `CppWinRTIncludes.h` already defines
  `comp = winrt::Microsoft::UI::Composition` at global scope; redefining it is
  C2881.
- **Event names must be `top*`-prefixed** to pass core's `normalizeEventType`
  unchanged; map them to `on*` registration names in your JS view config.

## Roadmap

1. Standalone compile validation of the header + example inside a fresh RNW
   0.83 cpp-lib project (CI matrix: x64/ARM64, Debug/Release).
2. A second production consumer: the live camera viewfinder in
   [FacilitronWorks/react-native-windows-camera](https://github.com/FacilitronWorks/react-native-windows-camera)
   (MediaFrameReader → Composition surface → this primitive).
3. Composition-visual variant (host a raw `Microsoft.UI.Composition.Visual`
   via `ContentIsland` for non-XAML content — the camera frame blitter wants
   this).
4. Async helper: a `ResumeOnQueue` awaiter + strong-ref discipline docs for
   subclasses with coroutines.
5. NuGet + npm packaging; propose the pattern (or the doc) upstream to
   react-native-windows as a samples/docs contribution.

## Relationship to upstream

- **react-native-windows** ships the hard part (`ContentIslandComponentView`).
  This repo is the missing "how to actually use it as a library author" layer.
  If RNW wants any of this (docs, header, example) upstream, it's MIT and we
  will happily PR it.
- **react-native-webview 15.0.0** independently validates the architecture;
  this repo exists so the next component (camera preview, video, maps, PDF)
  starts from a primitive instead of a 700-line reference implementation.

## License

MIT.
