# Verified RNW 0.83.2 / WinAppSDK 1.8 API surface

Every ABI call `XamlIslandComponent.h` makes was verified against
react-native-windows **0.83.2** sources on disk (July 2026) before the pattern
shipped in production. Paths are relative to `node_modules/react-native-windows/`
unless noted. This file is the receipts; the header is the code.

## Fabric component registration (the exact 0.83.2 ABI)

1. **`IReactPackageBuilderFabric.AddViewComponent(String componentName, ReactViewComponentProvider provider)`**
   — `Microsoft.ReactNative/IReactPackageBuilderFabric.idl:21`;
   implementation `Microsoft.ReactNative/ReactPackageBuilder.cpp:37`.
   There is **no attributed auto-registration** for view components: your
   package provider's `CreatePackage` must call it explicitly (which is what
   `XamlIslandComponentRegistrar::Register` does).

2. **`IReactViewComponentBuilder`** — `Microsoft.ReactNative/IReactViewComponentBuilder.idl:115-137`:
   `SetCreateProps` (`:118`), `SetCustomCommandHandler` (`:127`),
   `SetUpdatePropsHandler` (`:129`), `SetUpdateEventEmitterHandler` (`:131`),
   and — critically — **`Boolean XamlSupport { get; set; }`** (`:136`).

3. **`IComponentProps.SetProp(UInt32 hash, String propName, IJSValueReader value)`**
   — `Microsoft.ReactNative/ViewProps.idl:36-42`. Reader ABI:
   `GetString/GetBoolean/GetInt64/GetDouble/GetNextObjectProperty/GetNextArrayItem`
   — `Microsoft.ReactNative/IJSValueReader.idl:65-90`. (Note: there is no
   `GetInt()` — an early scaffold of ours called it, proof it had never been
   compiled. Use `GetInt64`.)

4. **`IReactCompositionViewComponentBuilder.SetContentIslandComponentViewInitializer(ComponentIslandComponentViewInitializer)`**
   — `Microsoft.ReactNative/IReactCompositionViewComponentBuilder.idl:71`
   (delegate at `:22`); implementation
   `Microsoft.ReactNative/Fabric/Composition/ReactCompositionViewComponentBuilder.cpp:96-114`
   (constructs RNW's internal `ContentIslandComponentView`, invokes your
   initializer with it).

5. **`ContentIslandComponentView : ViewComponentView { void Connect(Microsoft.UI.Content.ContentIsland); ChildSiteLink { get; } }`**
   — `Microsoft.ReactNative/CompositionComponentView.idl:94-98`;
   implementation `Fabric/Composition/ContentIslandComponentView.{h,cpp}`.

6. **`ComponentView`** base — `Microsoft.ReactNative/ComponentView.idl`:
   `Tag` (`:103`), `Parent` (`:104`), `ReactContext` (`:106`), and lifecycle
   events `Destroying` (`:127`), `LayoutMetricsChanged` (`:128`), `Mounted`
   (`:129`), `Unmounted` (`:130`). The registrar keys its per-instance registry
   on `Tag()` and tears down on `Destroying`.

7. **Event dispatch**: `EventEmitter.DispatchEvent(String, JSValueArgWriter)`
   — `IReactViewComponentBuilder.idl:106-108`; implementation
   `Microsoft.ReactNative/Fabric/AbiEventEmitter.cpp:11-19` forwards the name
   **verbatim** into core `facebook::react::EventEmitter::dispatchEvent`, whose
   `normalizeEventType`
   (`node_modules/react-native/ReactCommon/react/renderer/core/EventEmitter.cpp:29-39`)
   passes `top*`-prefixed names through unchanged. → Emit `topMyEvent` names
   and register exactly those in your JS view config with `registrationName`
   mappings.

## What RNW's ContentIslandComponentView already solves for you

| Hard problem | Where RNW 0.83.2 solves it (verified source) |
| --- | --- |
| Parent the island into the RN visual tree | `ChildSiteLink::Create(parentIsland, containerVisual-of-this-view)` — `Fabric/Composition/ContentIslandComponentView.cpp:53-63` |
| Size the hosted content | `updateLayoutMetrics` → `ChildSiteLink.ActualSize(...)` — same file, `:261-269` |
| Position, incl. **scrolling parents** | `ParentLayoutChanged` sets `LocalToParentTransformMatrix` synchronously; subscribes to every ancestor's `LayoutMetricsChanged` **and** every ancestor ScrollView's `ViewChanged` (issue #15557 fix present in 0.83.2) — `:108-133, 158-178` |
| DPI | transform computed as physical-px `getClientRect()` ÷ `pointScaleFactor` — `:170-177` |
| Focus/keyboard in+out | `InputFocusNavigationHost.GetForSiteLink` + `DepartFocusRequested` → `rootComponentView()->TryMoveFocus`; `onGotFocus` → `NavigateFocus` into the island — `:79-88, 229-237` |
| Accessibility | `ChildSiteLink.AutomationOption(FrameworkBased)` + fragment provider — `:180-185, 287-293` |
| Connect-before-mount races | `Connect()` queues the island until `OnMounted` — `:271-281` |

## The island handoff

**`Microsoft.UI.Xaml.XamlIsland`** exists in the WinAppSDK **1.8** projection:
default constructor, `Content(UIElement)` get/put, **`ContentIsland()`
getter**, `SystemBackdrop`, `IClosable` (verified in the generated
`winrt/Microsoft.UI.Xaml.h` of a WinAppSDK 1.8.260508005 app). The handoff is
three lines — no `DesktopWindowXamlSource`, no `WindowId`, no HWND:

```cpp
winrt::Microsoft::UI::Xaml::XamlIsland xamlIsland;   // pure island, no HWND site
xamlIsland.Content(myWinUIControl);                   // any UIElement tree
componentView.Connect(xamlIsland.ContentIsland());    // RNW re-sites it into the RN tree
```

## XAML framework bootstrap — RNW does it for you

`builder.XamlSupport(true)` marks the component as XAML-requiring
(`ReactCompositionViewComponentBuilder.cpp:238-243`,
`Fabric/WindowsComponentDescriptorRegistry.cpp:57-61`). On instance load,
`ReactNativeHost.cpp:105-109` (compiled in when `RNW_XAML_ISLAND` is defined —
it is, unconditionally, in `Microsoft.ReactNative.vcxproj:128`) calls
`XamlApplication::EnsureCreated()` (`Microsoft.ReactNative/XamlApplication.h:25-29`,
`.cpp:11-30`), which creates the `Microsoft.UI.Xaml.Application` singleton,
runs `WindowsXamlManager::InitializeForCurrentThread()`, and wires the WinUI
controls resource manager. **You write zero bootstrap code.** Cost: the XAML
framework initializes at app startup once any island component is registered,
mounted or not.

## Deliberate non-choice: composition controllers

For controls that expose a raw composition visual (e.g.
`CoreWebView2CompositionController.RootVisualTarget`), grafting the visual
directly into RNW's tree requires hand-rolled input forwarding, cursor
management, and focus plumbing. Hosting the XAML **control** in an island
reuses Microsoft's implementation of that machinery and RNW's implementation
of island siting. Prefer the island.
