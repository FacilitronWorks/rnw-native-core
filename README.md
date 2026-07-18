> **Alpha (`0.1.0-alpha.0`).** The hosting pattern is production-proven (it is
> how a shipping react-native-windows 0.83 new-architecture app renders WebView2
> today), and as of this release the packaged header **is compile-validated**:
> `include/rnwcore/XamlIslandComponent.h` and `example/BadgeView.cpp` compile
> **and link** against react-native-windows **0.83.2**, **ARM64 / Debug**, MSVC
> v143, C++20, as standalone translation units with no precompiled header. See
> [Build validation](#build-validation) for exactly what was and was not tested —
> including the two real bugs that validation caught. What is still unproven is
> *runtime* behavior of this generalized form: the island bring-up has been run
> in production only in its hand-written WebView2 ancestor, not through this
> template.

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

## Install

```sh
npm install --save-dev @facilitronworks/rnw-native-core
# or: yarn add -D @facilitronworks/rnw-native-core
```

**There is no JavaScript in this package — deliberately.** No `main`, no
`types`, no `react-native.config.js`. It is a single C++ header (plus its
example and its API receipts). A fake JS entrypoint would only serve to make
bundlers and `react-native config` walk into a package that has nothing for
them; autolinking has nothing to link, because there is no `.vcxproj` and
nothing to compile into a DLL. `npm install` here is purely a delivery
mechanism for a file that lands on your **include path**. `devDependencies` is
the honest place for it (nothing ships at runtime); a plain `dependencies` also
works if your tooling prunes dev deps in ways you'd rather avoid.

`react-native-windows` is declared as an **optional** peer (`>=0.83.0`) — the
ABI this header calls is the 0.83 New-Architecture one. Optional so that a
consuming *library* which lists RNW itself as a peer doesn't get a duplicate
install forced on it.

### Putting the header on your include path

Import the shipped property sheet from your library's (or app's) `.vcxproj` —
**one line**, anywhere after the standard `PropertySheets` `ImportGroup`:

```xml
<Import Project="$(SolutionDir)..\node_modules\@facilitronworks\rnw-native-core\build\rnw-native-core.props" />
```

That sheet prepends `…\rnw-native-core\include\` to
`ClCompile.AdditionalIncludeDirectories` (via `$(MSBuildThisFileDirectory)`, so
it is relocatable), exposes `$(RnwNativeCoreDir)` / `$(RnwNativeCoreVersion)`,
and hard-errors with a readable message if the header isn't where the import
path says it is. It sets nothing else — no toolset, no configuration, no target
platform — so it cannot fight the RNW sheets.

Then:

```cpp
#include <rnwcore/XamlIslandComponent.h>
```

**Why this is a manual one-liner and not automatic.** RNW's autolinking
(`@react-native-windows/cli`, `autolinkWindows.js`) only emits
`ProjectReference` entries into `AutolinkedNativeModules.g.props` plus
`#include`/package-provider glue into `AutolinkedNativeModules.g.cpp`, and it
does that **only for dependencies that ship a Windows project file**. It has no
concept of "contribute an include directory." A header-only package with no
`.vcxproj` therefore cannot be autolinked, and inventing a stub `.vcxproj`
purely to be autolinked would make every consumer build and link an empty DLL.
One explicit `<Import>` is the honest mechanism. (If you prefer no MSBuild edit
at all, vendoring `include/rnwcore/` still works exactly as it did — the header
has no build-system dependencies.)

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

## Build validation

`0.1.0-alpha.0` was validated by compiling the packaged header **as its own
translation unit, with no precompiled header**, inside a real RNW 0.83.2
C++/WinRT Fabric library project, and linking the resulting DLL:

- **Toolchain:** MSBuild 18 / MSVC v143, `LanguageStandard=stdcpp20`, Windows
  SDK 10.0.22621.0, WinAppSDK 1.8, `WarningLevel=Level4`.
- **Target:** `Configuration=Debug`, `Platform=ARM64`, against prebuilt
  `Microsoft.ReactNative` 0.83.2.
- **What was compiled:** a TU that includes `<rnwcore/XamlIslandComponent.h>`
  and *instantiates* `XamlIslandComponentRegistrar<T>::Register(...)` — which
  is what forces the template body and every lambda inside it through the
  compiler — plus `example/BadgeView.cpp` (`.h` + `.cpp`) built through the
  shipped `build/rnw-native-core.props` include path.
- **Result:** MSBuild exit code 0; both TUs compiled and the library linked.

Validation found and fixed **two real defects** that no amount of code review
had caught, both of the same family — a projection header that was being relied
on transitively:

1. `XamlIslandComponent.h` called `XamlIsland::Close()` without including
   `<winrt/Windows.Foundation.h>`. The `Microsoft.*` projections only
   forward-declare the `IClosable` `consume_` mixin, so a TU that hadn't
   already pulled in Windows.Foundation died with **C3779** (*"a function that
   returns `auto` cannot be used before it is defined"*). Consuming projects
   whose pch includes Windows.Foundation — i.e. every project that had ever
   used this code — never saw it. Fixed by including it in the header.
2. `example/BadgeView.cpp` used `UIElement::Tapped` with only
   `<winrt/Microsoft.UI.Xaml.Controls.h>` included. That declares
   `TappedEventHandler` but its lambda-wrapping templated constructor is
   *defined* in the Input projection, so it compiled clean and then failed at
   **LNK2019**. Fixed by including `<winrt/Microsoft.UI.Xaml.Input.h>`.

**Not yet validated** (do not read more into the badge than this):

- x64 / Win32 platforms and Release configuration — ARM64 Debug only so far.
- Runtime behavior *of this generalized template*. The island bring-up,
  registry, and teardown are line-for-line the production WebView2 component's,
  but they have been executed at runtime in that hand-written form, not through
  `XamlIslandComponentRegistrar<T>`. No mount/unmount/leak test has been run
  against this packaging.
- RNW versions other than 0.83.2.

## Status matrix

| Piece | Status |
| --- | --- |
| Hosting pattern (island bootstrap, Connect, registry, teardown) | ✅ **Production-proven** inside a shipping app's WebView component |
| ABI surface used | ✅ Verified against RNW 0.83.2 IDLs/sources, file:line cited in `docs/VERIFIED-API.md` |
| This header as packaged (generalized base class + registrar template) | ✅ **Compiles + links** standalone, RNW 0.83.2 / ARM64 Debug / MSVC v143 / C++20 (see [Build validation](#build-validation)) |
| `example/BadgeView` | ✅ Compiles + links standalone (same matrix) |
| Runtime behavior of the generalized template | ⏳ Unproven — the pattern runs in production, this packaging of it has not been mounted |
| CI matrix (x64/Win32, Release) | ⏳ Not run yet — ARM64 Debug only |
| Old-architecture hosts | ❌ Not supported (Composition/Fabric only) — guard registration with `RNW_NEW_ARCH` |
| npm distribution | ✅ `@facilitronworks/rnw-native-core` — header + `build/rnw-native-core.props`; vendoring `include/rnwcore/` still works |
| NuGet distribution | ❌ Not yet |
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
- **Never rely on a pch for a projection header.** Both defects the first
  compile validation found were "it worked because the consuming project's pch
  happened to include it": `Windows.Foundation` for `IClosable::Close`
  (C3779 at compile time) and `Microsoft.UI.Xaml.Input` for the
  `TappedEventHandler` lambda constructor (LNK2019 at link time — clean
  compile, dead link). Include what you call, in the header that calls it.
- **Event names must be `top*`-prefixed** to pass core's `normalizeEventType`
  unchanged; map them to `on*` registration names in your JS view config.

## Roadmap

1. ~~Standalone compile validation of the header + example~~ — **done** for
   ARM64/Debug (see [Build validation](#build-validation)); still to do: the
   full CI matrix (x64/Win32, Release) and a runtime mount/unmount test of the
   generalized template.
2. A second production consumer: the live camera viewfinder in
   [FacilitronWorks/react-native-windows-camera](https://github.com/FacilitronWorks/react-native-windows-camera)
   (MediaFrameReader → Composition surface → this primitive).
3. Composition-visual variant (host a raw `Microsoft.UI.Composition.Visual`
   via `ContentIsland` for non-XAML content — the camera frame blitter wants
   this).
4. Async helper: a `ResumeOnQueue` awaiter + strong-ref discipline docs for
   subclasses with coroutines.
5. ~~npm packaging~~ — **done** (`@facilitronworks/rnw-native-core`). Still
   open: NuGet packaging, and proposing the pattern (or the doc) upstream to
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
