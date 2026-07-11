#pragma once
// ============================================================================
// Example: hosting a plain WinUI 3 TextBlock as a Fabric component using
// rnwcore::XamlIslandComponent. ~60 lines of subclass code buys you a native
// XAML control that sizes, clips, scrolls, and focuses correctly inside the RN
// tree.
//
// JS side (view config sketch):
//   const Badge = NativeComponentRegistry.get('RnwBadge', () => ({
//     uiViewClassName: 'RnwBadge',
//     validAttributes: { label: true },
//     directEventTypes: {
//       topTapped: { registrationName: 'onTapped' },
//     },
//   }))
//   <Badge label="42" onTapped={…} style={{ width: 48, height: 24 }} />
//
// Registration (package provider):
//   rnwcore::XamlIslandComponentRegistrar<rnwcore_example::BadgeView>::Register(
//       packageBuilder, L"RnwBadge", &rnwcore_example::MakeBadgeProps);
// ============================================================================

#include <rnwcore/XamlIslandComponent.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace rnwcore_example
{
    // Custom props bag: one string prop, "label". Unknown props are consumed
    // and skipped so the reader never desyncs.
    winrt::Microsoft::ReactNative::IComponentProps MakeBadgeProps(
        winrt::Microsoft::ReactNative::ViewProps props,
        winrt::Microsoft::ReactNative::IComponentProps cloneFrom);

    class BadgeView final : public rnwcore::XamlIslandComponent
    {
    public:
        winrt::Microsoft::UI::Xaml::UIElement CreateContent() override;

        void OnPropsUpdated(
            winrt::Microsoft::ReactNative::IComponentProps const &newProps,
            winrt::Microsoft::ReactNative::IComponentProps const &oldProps) noexcept override;

        void OnCommand(
            winrt::hstring const &commandName,
            winrt::Microsoft::ReactNative::IJSValueReader const &args) noexcept override;

        void OnDestroying() noexcept override;

    private:
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_text{nullptr};
        winrt::event_token m_tappedToken{};
    };
}
