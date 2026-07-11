#pragma once
// ============================================================================
// rnwcore::XamlIslandComponent — the missing primitive for react-native-windows
// New Architecture: host ANY WinUI 3 / XAML control inside the RN composition
// tree as a Fabric component.
//
//   your WinUI control (any Microsoft.UI.Xaml.UIElement)
//     -> Microsoft.UI.Xaml.XamlIsland          (pure island, no HWND site)
//     -> island.ContentIsland()
//     -> RNW ContentIslandComponentView.Connect()
//        (ChildSiteLink: size / position / scroll-transform / clipping /
//         DPI / focus / UIA — all maintained by RNW, verified 0.83.2)
//
// This header packages the hosting pattern PROVEN IN PRODUCTION by our WebView2
// Fabric component (FacilitronWorks/react-native-webview-windows — web content
// rendering inside a shipping RNW 0.83.2 new-arch app since July 2026), and
// independently converged on by upstream react-native-webview 15.0.0's in-tree
// Windows implementation. See docs/VERIFIED-API.md for the exact RNW file:line
// citations behind every ABI call made here.
//
// WHAT YOU GET
//   - registration via IReactPackageBuilderFabric.AddViewComponent +
//     IReactCompositionViewComponentBuilder.SetContentIslandComponentViewInitializer
//   - builder.XamlSupport(true) so RNW bootstraps the WinUI 3 XAML framework
//     for you at instance init (no app-side XamlApplication code)
//   - the island bootstrap (XamlIsland -> Content -> Connect)
//   - a Tag-keyed per-mount instance registry
//   - Destroying-hooked teardown that erases the registry entry, calls your
//     OnDestroying(), and Close()s the island. DO NOT SKIP THIS: leaked
//     islands are known to break input routing window-wide.
//
// WHAT YOU WRITE
//   - a default-constructible subclass overriding CreateContent() (return the
//     WinUI element to host) and, optionally, OnPropsUpdated / OnCommand /
//     OnDestroying;
//   - one registration line in your package provider's CreatePackage:
//       rnwcore::XamlIslandComponentRegistrar<MyView>::Register(
//           packageBuilder, L"MyView");
//
// REQUIREMENTS / CAVEATS (hard-won, all hit in production):
//   - New Architecture (Fabric/Composition) only. Guard registration with
//     RNW_NEW_ARCH if your package must also load on old-arch hosts.
//   - Everything runs on the RNW UI thread (Fabric callbacks arrive there;
//     XAML islands are thread-affine to it). If you co_await WinRT async ops
//     in a subclass, re-assert that thread after each hop (capture
//     Microsoft.UI.Dispatching.DispatcherQueue::GetForCurrentThread() in
//     CreateContent()).
//   - Do NOT declare `namespace comp = winrt::Microsoft::ReactNative::
//     Composition;` in a TU that includes RNW's CppWinRTIncludes.h — RNW
//     already defines `comp` = Microsoft::UI::Composition at global scope
//     (C2881). Use a different alias (this header uses rnwcore_rncomp
//     internally via fully-qualified names).
//   - STATUS: this standalone header has not been compile-validated yet; it is
//     a faithful generalization of component code that compiles and runs in a
//     production app. See README "Build validation".
// ============================================================================

#include <winrt/Microsoft.ReactNative.h>
#include <winrt/Microsoft.ReactNative.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace rnwcore
{
    // ------------------------------------------------------------------------
    // Base class for one mounted instance of your component. One object is
    // created per JS mount and destroyed on the view's Destroying event.
    //
    // Overridable hooks are public so the registrar template needs no
    // friendship gymnastics; they are only ever invoked by the registrar.
    // ------------------------------------------------------------------------
    class XamlIslandComponent
    {
    public:
        virtual ~XamlIslandComponent() = default;

        // --- subclass surface -------------------------------------------------

        // REQUIRED. Create and return the WinUI element to host. Called once,
        // on the RNW UI thread, before the island is connected. `View()` is
        // already valid here.
        virtual winrt::Microsoft::UI::Xaml::UIElement CreateContent() = 0;

        // Optional. Fabric prop update. Cast to your IComponentProps
        // implementation (registered via the propsFactory argument of
        // Register()).
        virtual void OnPropsUpdated(
            winrt::Microsoft::ReactNative::IComponentProps const & /*newProps*/,
            winrt::Microsoft::ReactNative::IComponentProps const & /*oldProps*/) noexcept
        {
        }

        // Optional. Imperative command dispatched from JS
        // (UIManager.dispatchViewManagerCommand / codegenNativeCommands).
        virtual void OnCommand(
            winrt::hstring const & /*commandName*/,
            winrt::Microsoft::ReactNative::IJSValueReader const & /*args*/) noexcept
        {
        }

        // Optional. Called on unmount BEFORE the island is closed. Release
        // device/OS resources and revoke event tokens here. After this returns,
        // Emitter()/View()/Island() are nulled — a late async completion in
        // your subclass must tolerate that (hold shared_from_this-style strong
        // refs across co_awaits if needed).
        virtual void OnDestroying() noexcept {}

        // --- state available to subclasses -------------------------------------

        winrt::Microsoft::ReactNative::Composition::ContentIslandComponentView const &View()
            const noexcept
        {
            return m_componentView;
        }

        winrt::Microsoft::UI::Xaml::XamlIsland const &Island() const noexcept
        {
            return m_island;
        }

        // The Fabric event emitter. Dispatch native -> JS events as
        //   Emitter().DispatchEvent(L"topMyEvent", [](IJSValueWriter const &w) {…});
        // Names MUST be "top…"-prefixed (RNW's AbiEventEmitter forwards them
        // verbatim; core normalizeEventType passes "top*" through), and your JS
        // view config maps them to on… registration names. May be null before
        // the first SetUpdateEventEmitterHandler call and after teardown —
        // always null-check.
        winrt::Microsoft::ReactNative::EventEmitter const &Emitter() const noexcept
        {
            return m_eventEmitter;
        }

        // --- driven by the registrar (public by design; do not call yourself) --

        void RnwcoreInitialize(
            winrt::Microsoft::ReactNative::Composition::ContentIslandComponentView const &view)
        {
            m_componentView = view;
            // Pure-island XAML host (WinAppSDK 1.8+) — no HWND site involved.
            // Requires the XAML framework on this thread; RNW guarantees that
            // via XamlSupport(true) -> XamlApplication::EnsureCreated().
            m_island = winrt::Microsoft::UI::Xaml::XamlIsland{};
            m_island.Content(CreateContent());
            // RNW queues the island until the view mounts, then creates the
            // ChildSiteLink, sizes/positions it (incl. ancestor scroll tracking
            // + DPI), and connects.
            view.Connect(m_island.ContentIsland());
        }

        void RnwcoreSetEmitter(
            winrt::Microsoft::ReactNative::EventEmitter const &emitter) noexcept
        {
            m_eventEmitter = emitter;
        }

        void RnwcoreTeardown() noexcept
        {
            try
            {
                OnDestroying();
            }
            catch (...)
            {
            }
            try
            {
                if (m_island)
                {
                    m_island.Close(); // IClosable: releases the island + hosted tree
                }
            }
            catch (...)
            {
            }
            m_island = nullptr;
            m_eventEmitter = nullptr;
            m_componentView = nullptr;
        }

    private:
        winrt::Microsoft::ReactNative::Composition::ContentIslandComponentView
            m_componentView{nullptr};
        winrt::Microsoft::ReactNative::EventEmitter m_eventEmitter{nullptr};
        winrt::Microsoft::UI::Xaml::XamlIsland m_island{nullptr};
    };

    // ------------------------------------------------------------------------
    // Registrar. TComponent: default-constructible subclass of
    // XamlIslandComponent. One registry (Tag -> instance) per TComponent.
    // ------------------------------------------------------------------------
    template <typename TComponent>
    struct XamlIslandComponentRegistrar
    {
        // Optional custom props factory: return your winrt::implements<…,
        // IComponentProps> bag. Pass nullptr to keep RNW's default ViewProps.
        using PropsFactory = winrt::Microsoft::ReactNative::IComponentProps (*)(
            winrt::Microsoft::ReactNative::ViewProps,
            winrt::Microsoft::ReactNative::IComponentProps);

        // Call ONCE from your IReactPackageProvider::CreatePackage.
        // componentName must match the name your JS mounts
        // (NativeComponentRegistry.get / codegen native component name).
        static void Register(
            winrt::Microsoft::ReactNative::IReactPackageBuilder const &packageBuilder,
            winrt::hstring const &componentName,
            PropsFactory propsFactory = nullptr) noexcept
        {
            using namespace winrt::Microsoft::ReactNative;
            namespace rncomp = winrt::Microsoft::ReactNative::Composition;

            auto fabricBuilder = packageBuilder.as<IReactPackageBuilderFabric>();

            fabricBuilder.AddViewComponent(
                componentName,
                [propsFactory](IReactViewComponentBuilder const &builder) noexcept {
                    // RNW bootstraps the WinUI 3 XAML framework (XamlApplication
                    // + WindowsXamlManager::InitializeForCurrentThread) at
                    // instance init because a registered component requires it.
                    builder.XamlSupport(true);

                    if (propsFactory)
                    {
                        builder.SetCreateProps(
                            [propsFactory](ViewProps props, IComponentProps cloneFrom) noexcept
                                -> IComponentProps { return propsFactory(props, cloneFrom); });
                    }

                    auto compBuilder = builder.as<rncomp::IReactCompositionViewComponentBuilder>();
                    compBuilder.SetContentIslandComponentViewInitializer(
                        [](rncomp::ContentIslandComponentView const &view) noexcept {
                            auto instance = std::make_shared<TComponent>();
                            {
                                std::scoped_lock lock(Mutex());
                                Map()[view.Tag()] = instance;
                            }
                            // Teardown hook: when RN destroys the view (unmount
                            // / instance shutdown), erase the registry entry
                            // and tear the instance down. Keyed by the stable
                            // Fabric Tag — no session-long growth, no stale
                            // event dispatch after unmount.
                            view.Destroying(
                                [](winrt::Windows::Foundation::IInspectable const &,
                                   ComponentView const &v) noexcept {
                                    std::shared_ptr<TComponent> dying;
                                    {
                                        std::scoped_lock lock(Mutex());
                                        auto it = Map().find(v.Tag());
                                        if (it != Map().end())
                                        {
                                            dying = std::move(it->second);
                                            Map().erase(it);
                                        }
                                    }
                                    if (dying)
                                    {
                                        dying->RnwcoreTeardown();
                                    }
                                });
                            try
                            {
                                instance->RnwcoreInitialize(view);
                            }
                            catch (...)
                            {
                                // Island bring-up failed (e.g. XAML framework
                                // missing). Leave the instance registered but
                                // inert; the view renders empty instead of
                                // crashing the host.
                            }
                        });

                    builder.SetUpdatePropsHandler(
                        [](ComponentView const &source, IComponentProps const &newProps,
                           IComponentProps const &oldProps) noexcept {
                            if (auto instance = Find(source))
                            {
                                instance->OnPropsUpdated(newProps, oldProps);
                            }
                        });

                    builder.SetUpdateEventEmitterHandler(
                        [](ComponentView const &source, EventEmitter const &eventEmitter) noexcept {
                            if (auto instance = Find(source))
                            {
                                instance->RnwcoreSetEmitter(eventEmitter);
                            }
                        });

                    builder.SetCustomCommandHandler(
                        [](ComponentView const &source, HandleCommandArgs const &args) noexcept {
                            if (auto instance = Find(source))
                            {
                                instance->OnCommand(args.CommandName(), args.CommandArgs());
                                args.Handled(true);
                            }
                        });
                });
        }

        // Look up the live instance for a Fabric callback's source view.
        static std::shared_ptr<TComponent> Find(
            winrt::Microsoft::ReactNative::ComponentView const &view) noexcept
        {
            try
            {
                std::scoped_lock lock(Mutex());
                auto it = Map().find(view.Tag());
                return it == Map().end() ? nullptr : it->second;
            }
            catch (...)
            {
                return nullptr;
            }
        }

    private:
        static std::mutex &Mutex() noexcept
        {
            static std::mutex s_mutex;
            return s_mutex;
        }
        static std::unordered_map<int32_t, std::shared_ptr<TComponent>> &Map() noexcept
        {
            static std::unordered_map<int32_t, std::shared_ptr<TComponent>> s_map;
            return s_map;
        }
    };
} // namespace rnwcore
