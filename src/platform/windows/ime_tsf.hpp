// Windows TSF (Text Services Framework) input context (architecture §8.3).
//
// The text store is owned by TSF once attached to an ITfContext; the context
// holds the only strong reference after construction. Composition and commit
// are surfaced as ordinary Continui Events, never as raw key events.
#pragma once

#include <continui/events.hpp>
#include <continui/geometry.hpp>

#include <windows.h>

#include <functional>
#include <memory>
#include <string_view>

namespace continui::detail {

class TsfInputContext {
public:
    using EventSink = std::function<void(const Event&)>;

    TsfInputContext(HWND hwnd, EventSink sink);
    ~TsfInputContext();

    TsfInputContext(const TsfInputContext&) = delete;
    TsfInputContext& operator=(const TsfInputContext&) = delete;

    // Window focus drives TSF activation (architecture §8.3 focus sync).
    void on_focus(bool focused) noexcept;

    // Keep the TSF document model in sync with the UI's focused input: the
    // current text plus the caret rectangle in client pixels. Used for
    // candidate-window positioning and caret/selection sync (§8.3, audit F5).
    void sync_state(std::string_view utf8_text, Rect caret,
                    Rect text_bounds) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace continui::detail
