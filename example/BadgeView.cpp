// Example subclass — see BadgeView.h. Compile-validated (compiles AND links)
// against react-native-windows 0.83.2, ARM64 Debug, MSVC v143 / C++20.
#include "BadgeView.h"

// Required for UIElement::Tapped: <winrt/Microsoft.UI.Xaml.Controls.h> only
// declares TappedEventHandler; its templated constructor (the one that wraps
// your lambda) is *defined* in the Input projection. Without this you compile
// clean and then fail at link with
//   LNK2019: unresolved external symbol ... TappedEventHandler::TappedEventHandler<lambda>
// Same class of trap as the Windows.Foundation include in the core header.
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <string>

using namespace winrt;
using namespace winrt::Microsoft::ReactNative;
namespace mux = winrt::Microsoft::UI::Xaml;

namespace
{
    // Fully consume the value the reader is on (keeps the reader in sync for
    // unknown props).
    void SkipValue(IJSValueReader const &r) noexcept
    {
        try
        {
            switch (r.ValueType())
            {
            case JSValueType::String: r.GetString(); break;
            case JSValueType::Boolean: r.GetBoolean(); break;
            case JSValueType::Int64: r.GetInt64(); break;
            case JSValueType::Double: r.GetDouble(); break;
            case JSValueType::Object: { winrt::hstring k; while (r.GetNextObjectProperty(k)) SkipValue(r); break; }
            case JSValueType::Array: while (r.GetNextArrayItem()) SkipValue(r); break;
            default: break;
            }
        }
        catch (...) {}
    }

    struct BadgeProps : winrt::implements<BadgeProps, IComponentProps>
    {
        BadgeProps(ViewProps props, IComponentProps cloneFrom) : m_viewProps(props)
        {
            if (cloneFrom)
            {
                auto prior = cloneFrom.as<BadgeProps>();
                label = prior->label;
            }
        }

        void SetProp(uint32_t /*hash*/, winrt::hstring propName, IJSValueReader value) noexcept
        {
            if (std::wstring{propName} == L"label")
                label = value.ValueType() == JSValueType::String ? value.GetString() : winrt::hstring{};
            else
                SkipValue(value);
        }

        ViewProps m_viewProps{nullptr};
        winrt::hstring label;
    };
}

namespace rnwcore_example
{
    IComponentProps MakeBadgeProps(ViewProps props, IComponentProps cloneFrom)
    {
        return winrt::make<BadgeProps>(props, cloneFrom);
    }

    mux::UIElement BadgeView::CreateContent()
    {
        m_text = mux::Controls::TextBlock{};
        m_text.HorizontalAlignment(mux::HorizontalAlignment::Center);
        m_text.VerticalAlignment(mux::VerticalAlignment::Center);

        // Native -> JS event: forward WinUI Tapped as topTapped/onTapped.
        m_tappedToken = m_text.Tapped(
            [this](winrt::Windows::Foundation::IInspectable const &, auto const &) {
                if (auto const &emitter = Emitter())
                {
                    emitter.DispatchEvent(L"topTapped", [](IJSValueWriter const &writer) {
                        writer.WriteObjectBegin();
                        writer.WriteObjectEnd();
                    });
                }
            });

        return m_text;
    }

    void BadgeView::OnPropsUpdated(
        IComponentProps const &newProps, IComponentProps const & /*oldProps*/) noexcept
    {
        if (!m_text || !newProps)
            return;
        try
        {
            auto p = newProps.as<BadgeProps>();
            m_text.Text(p->label);
        }
        catch (...)
        {
        }
    }

    void BadgeView::OnCommand(
        winrt::hstring const &commandName, IJSValueReader const &args) noexcept
    {
        // One imperative command: setLabel(text)
        if (std::wstring{commandName} != L"setLabel" || !m_text)
            return;
        try
        {
            if (args.ValueType() == JSValueType::Array && args.GetNextArrayItem() &&
                args.ValueType() == JSValueType::String)
            {
                m_text.Text(args.GetString());
            }
        }
        catch (...)
        {
        }
    }

    void BadgeView::OnDestroying() noexcept
    {
        // Revoke event tokens BEFORE the base closes the island.
        try
        {
            if (m_text && m_tappedToken)
                m_text.Tapped(m_tappedToken);
        }
        catch (...)
        {
        }
        m_text = nullptr;
    }
}
