// Windows TSF text store (architecture §8.3).
//
// Minimal application-side TSF integration: one ITextStoreACP attached to an
// ITfContext plus ITfContextOwnerCompositionSink. TSF drives composition; the
// store surfaces start/update/commit as ordinary Events, never raw key events.
#include "ime_tsf.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <msctf.h>
#include <textstor.h>

#include <algorithm>
#include <cwchar>
#include <string>

namespace continui::detail {
namespace {

std::string to_utf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                          static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    if (bytes > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                            static_cast<int>(wide.size()), out.data(), bytes,
                            nullptr, nullptr);
    }
    return out;
}

// Use a uniquely named constant: defining IID_ITfContextOwnerCompositionSink
// with external linkage would collide if uuid/dxguid is linked later (F11).
constexpr IID kIidTfContextOwnerCompositionSink = {
    0x5F20AA40, 0xB57A, 0x4F34, {0x96, 0xAB, 0x35, 0x76, 0xF3, 0x77, 0xCC, 0x79}};
constexpr IID kIidTfContextOwnerServices = {
    0xb23eb630, 0x3e1c, 0x11d3, {0xa7, 0x45, 0x00, 0x50, 0x40, 0xab, 0x40, 0x07}};

std::wstring from_utf8(const std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int chars = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                          static_cast<int>(utf8.size()),
                                          nullptr, 0);
    std::wstring out(static_cast<std::size_t>(chars), L'\0');
    if (chars > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                            static_cast<int>(utf8.size()), out.data(), chars);
    }
    return out;
}

using CreateThreadMgrFn = HRESULT(WINAPI*)(ITfThreadMgr**);

using EmitFn = std::function<void(EventType, std::string)>;

class TextStore final : public ITextStoreACP,
                        public ITfContextOwnerCompositionSink {
public:
    TextStore(HWND hwnd, EmitFn emit)
        : ref_(1), hwnd_(hwnd), emit_(std::move(emit)) {}

    ~TextStore() {
        if (sink_ != nullptr) {
            sink_->Release();
        }
    }

    void set_edit_cookie(TfEditCookie cookie) noexcept { cookie_ = cookie; }

    // Sync the TSF document with the UI's focused input (0.7 audit F5).
    void sync_state(const std::string_view utf8, const Rect caret,
                    const Rect text_bounds) noexcept {
        const std::wstring updated = from_utf8(utf8);
        if (updated != text_) {
            text_ = updated;
            // Only reset TSF selection when the text actually changed;
            // otherwise each paint would clobber TSF's own selection (F5/N2).
            selection_start_ = selection_end_ = text_.size();
        }
        caret_rect_ = caret;
        text_rect_ = text_bounds;
    }

    // ---- IUnknown -----------------------------------------------------
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_ITextStoreACP) {
            *ppv = static_cast<ITextStoreACP*>(this);
        } else if (riid == kIidTfContextOwnerCompositionSink) {
            *ppv = static_cast<ITfContextOwnerCompositionSink*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG remaining = --ref_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    // ---- ITextStoreACP ------------------------------------------------
    STDMETHODIMP AdviseSink(REFIID, IUnknown* sink, DWORD) noexcept override {
        // COM requires the store to hold its own reference to the advise sink
        // (0.7 audit F7); TSF calls this from ITfDocumentMgr::Push.
        if (sink_ != nullptr) {
            sink_->Release();
        }
        sink_ = sink;
        if (sink_ != nullptr) {
            sink_->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(IUnknown* sink) noexcept override {
        if (sink_ != nullptr && (sink == nullptr || sink == sink_)) {
            sink_->Release();
            sink_ = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP RequestLock(DWORD flags, HRESULT* phr) noexcept override {
        if (phr == nullptr) {
            return E_POINTER;
        }
        *phr = S_OK;
        if (sink_ != nullptr) {
            ITextStoreACPSink* acp = nullptr;
            if (SUCCEEDED(sink_->QueryInterface(IID_ITextStoreACPSink,
                                                reinterpret_cast<void**>(&acp)))) {
                acp->OnLockGranted(flags);
                acp->Release();
            }
        }
        return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS* status) noexcept override {
        if (status == nullptr) {
            return E_POINTER;
        }
        status->dwDynamicFlags = 0;
        status->dwStaticFlags = TS_SS_NOHIDDENTEXT;
        return S_OK;
    }
    STDMETHODIMP QueryInsert(LONG start, LONG end, ULONG,
                             LONG* result_start, LONG* result_end) noexcept override {
        if (!result_start || !result_end) {
            return E_POINTER;
        }
        *result_start = start;
        *result_end = end;
        return S_OK;
    }
    STDMETHODIMP GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection,
                              ULONG* fetched) noexcept override {
        if (!fetched) {
            return E_POINTER;
        }
        *fetched = 0;
        if (index == 0 && count >= 1 && selection != nullptr) {
            selection[0].acpStart = static_cast<LONG>(selection_start_);
            selection[0].acpEnd = static_cast<LONG>(selection_end_);
            selection[0].style.ase = TS_AE_END;
            selection[0].style.fInterimChar = FALSE;
            *fetched = 1;
        }
        return S_OK;
    }
    STDMETHODIMP SetSelection(ULONG count,
                              const TS_SELECTION_ACP* selection) noexcept override {
        if (count == 0 || selection == nullptr) {
            return S_OK;
        }
        selection_start_ = static_cast<std::size_t>(
            std::clamp<LONG>(selection[0].acpStart, 0,
                             static_cast<LONG>(text_.size())));
        selection_end_ = static_cast<std::size_t>(
            std::clamp<LONG>(selection[0].acpEnd, 0,
                             static_cast<LONG>(text_.size())));
        return S_OK;
    }
    STDMETHODIMP GetText(LONG start, LONG end, WCHAR* plain, ULONG cch_req,
                         ULONG* pcch_ret, TS_RUNINFO*, ULONG, ULONG* pcRunRet,
                         LONG* next) noexcept override {
        if (!pcch_ret || !next) {
            return E_POINTER;
        }
        const LONG length = static_cast<LONG>(text_.size());
        const LONG from = std::clamp(start, 0L, length);
        const LONG to = end < 0 ? length : std::clamp(end, from, length);
        const ULONG count = static_cast<ULONG>(to - from);
        *pcch_ret = (std::min)(count, cch_req);
        if (plain != nullptr && *pcch_ret > 0) {
            std::copy_n(text_.begin() + from, *pcch_ret, plain);
        }
        if (pcRunRet != nullptr) {
            *pcRunRet = 0;
        }
        *next = to;
        return S_OK;
    }
    STDMETHODIMP SetText(DWORD, LONG start, LONG end, const WCHAR* chars,
                         ULONG count, TS_TEXTCHANGE* change) noexcept override {
        if (chars == nullptr && count != 0) {
            return E_POINTER;
        }
        const LONG length = static_cast<LONG>(text_.size());
        const LONG from = std::clamp(start, 0L, length);
        const LONG to = end < 0 ? length : std::clamp(end, from, length);
        const std::wstring inserted(chars == nullptr ? L"" : chars, count);
        text_.replace(static_cast<std::size_t>(from),
                      static_cast<std::size_t>(to - from), inserted);
        if (change != nullptr) {
            change->acpStart = from;
            change->acpOldEnd = to;
            change->acpNewEnd = from + static_cast<LONG>(count);
        }
        selection_start_ = selection_end_ = text_.size();
        if (!inserted.empty()) {
            last_text_ = to_utf8(inserted);
            emit_(EventType::composition_update, last_text_);
        }
        return S_OK;
    }
    STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject**) noexcept override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) noexcept override {
        return E_NOTIMPL;
    }
    STDMETHODIMP QueryInsertEmbedded(const GUID*, const FORMATETC*,
                                     BOOL* insertable) noexcept override {
        if (insertable) {
            *insertable = FALSE;
        }
        return S_OK;
    }
    STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject*,
                                TS_TEXTCHANGE*) noexcept override {
        return E_NOTIMPL;
    }
    STDMETHODIMP InsertTextAtSelection(DWORD, const WCHAR* chars, ULONG count,
                                       LONG* start, LONG* end,
                                       TS_TEXTCHANGE* change) noexcept override {
        if (!start || !end || (chars == nullptr && count != 0)) {
            return E_POINTER;
        }
        const LONG at = static_cast<LONG>(selection_end_);
        const std::wstring inserted(chars == nullptr ? L"" : chars, count);
        text_.insert(static_cast<std::size_t>(at), inserted);
        *start = at;
        *end = at + static_cast<LONG>(count);
        selection_start_ = selection_end_ = text_.size();
        if (change != nullptr) {
            change->acpStart = at;
            change->acpOldEnd = at;
            change->acpNewEnd = *end;
        }
        // InsertTextAtSelection is the authoritative commit path; remember it
        // so OnEndComposition does not insert the same text twice (F9).
        emitted_text_input_ = true;
        emit_(EventType::text_input, to_utf8(inserted));
        return S_OK;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*,
                                           TS_TEXTCHANGE*) noexcept override {
        return E_NOTIMPL;
    }
    STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) noexcept override {
        return S_OK;
    }
    STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) noexcept override {
        return S_OK;
    }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*,
                                                     DWORD) noexcept override {
        return S_OK;
    }
    STDMETHODIMP FindNextAttrTransition(LONG, LONG, ULONG, const TS_ATTRID*, DWORD,
                                        LONG* next, BOOL* found, LONG* offset) noexcept override {
        if (next) *next = 0;
        if (found) *found = FALSE;
        if (offset) *offset = 0;
        return S_OK;
    }
    STDMETHODIMP RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) noexcept override {
        if (fetched) *fetched = 0;
        return S_OK;
    }
    STDMETHODIMP GetEndACP(LONG* acp) noexcept override {
        if (!acp) return E_POINTER;
        *acp = static_cast<LONG>(text_.size());
        return S_OK;
    }
    STDMETHODIMP GetActiveView(TsViewCookie* view) noexcept override {
        if (!view) return E_POINTER;
        *view = 0;
        return S_OK;
    }
    STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT* point, DWORD,
                                 LONG* acp) noexcept override {
        if (!acp) return E_POINTER;
        // TSF passes SCREEN coordinates; the store's rects are CLIENT pixels.
        if (point != nullptr && text_rect_.width > 0.0f) {
            POINT client{point->x, point->y};
            ScreenToClient(hwnd_, &client);
            const float cursor_x = static_cast<float>(client.x);
            if (cursor_x <= text_rect_.x) {
                *acp = 0;
            } else if (cursor_x >= text_rect_.right()) {
                *acp = static_cast<LONG>(text_.size());
            } else {
                const float ratio = (cursor_x - text_rect_.x) / text_rect_.width;
                *acp = static_cast<LONG>(ratio * static_cast<float>(text_.size()));
            }
        } else {
            *acp = static_cast<LONG>(text_.size());
        }
        return S_OK;
    }
    STDMETHODIMP GetTextExt(TsViewCookie, LONG acp_start, LONG acp_end,
                            RECT* rect, BOOL* clipped) noexcept override {
        if (!rect) return E_POINTER;
        // TSF expects SCREEN coordinates. Return the full text extent for the
        // requested ACP range (single-line input approximation, F5/N1/N2).
        const Rect& source = text_rect_.width > 0.0f && text_rect_.height > 0.0f
                                 ? text_rect_
                                 : caret_rect_;
        if (source.width > 0.0f && source.height > 0.0f) {
            POINT top_left{static_cast<LONG>(source.x), static_cast<LONG>(source.y)};
            POINT bottom_right{static_cast<LONG>(source.right()),
                               static_cast<LONG>(source.bottom())};
            ClientToScreen(hwnd_, &top_left);
            ClientToScreen(hwnd_, &bottom_right);
            rect->left = top_left.x;
            rect->top = top_left.y;
            rect->right = bottom_right.x;
            rect->bottom = bottom_right.y;
        } else {
            GetWindowRect(hwnd_, rect);  // already screen coordinates
        }
        (void)acp_start;
        (void)acp_end;
        if (clipped) *clipped = FALSE;
        return S_OK;
    }
    STDMETHODIMP GetScreenExt(TsViewCookie, RECT* rect) noexcept override {
        if (!rect) return E_POINTER;
        RECT client{};
        GetClientRect(hwnd_, &client);
        // Convert BOTH corners via explicit POINTs (F5: reinterpret-cast of
        // RECT to POINT left right/bottom in client coordinates).
        POINT top_left{client.left, client.top};
        POINT bottom_right{client.right, client.bottom};
        ClientToScreen(hwnd_, &top_left);
        ClientToScreen(hwnd_, &bottom_right);
        rect->left = top_left.x;
        rect->top = top_left.y;
        rect->right = bottom_right.x;
        rect->bottom = bottom_right.y;
        return S_OK;
    }
    STDMETHODIMP GetWnd(TsViewCookie, HWND* window) noexcept override {
        if (!window) return E_POINTER;
        *window = hwnd_;
        return S_OK;
    }

    // ---- ITfContextOwnerCompositionSink -------------------------------
    STDMETHODIMP OnStartComposition(ITfCompositionView*, BOOL* ok) noexcept override {
        if (ok) *ok = TRUE;
        last_text_.clear();
        emitted_text_input_ = false;
        emit_(EventType::composition_start, {});
        return S_OK;
    }
    STDMETHODIMP OnUpdateComposition(ITfCompositionView*, ITfRange*) noexcept override {
        emit_(EventType::composition_update, last_text_);
        return S_OK;
    }
    STDMETHODIMP OnEndComposition(ITfCompositionView*) noexcept override {
        // If a text_input already committed this composition, send an empty
        // payload — the UI then only clears the preview (single commit path).
        emit_(EventType::composition_end,
              emitted_text_input_ ? std::string{} : last_text_);
        last_text_.clear();
        emitted_text_input_ = false;
        return S_OK;
    }

private:
    ULONG ref_;
    HWND hwnd_;
    EmitFn emit_;
    IUnknown* sink_ = nullptr;  // AddRef'd in AdviseSink, released on unadvise/dtor
    TfEditCookie cookie_ = TF_INVALID_EDIT_COOKIE;
    std::wstring text_;
    std::size_t selection_start_ = 0;
    std::size_t selection_end_ = 0;
    std::string last_text_;
    bool emitted_text_input_ = false;
    Rect caret_rect_{};   // client pixels, synced from UI (F5)
    Rect text_rect_{};    // client pixels, full text extent (candidate anchor)
};

}  // namespace

struct TsfInputContext::Impl {
    HWND hwnd = nullptr;
    EventSink sink;
    ITfThreadMgr* thread_mgr = nullptr;
    ITfDocumentMgr* doc_mgr = nullptr;
    ITfContext* context = nullptr;
    TextStore* store = nullptr;
    TextStore* text_store_weak = nullptr;  // non-owning; context owns the store
    TfClientId client_id = TF_CLIENTID_NULL;
    TfEditCookie edit_cookie = TF_INVALID_EDIT_COOKIE;
    HMODULE msctf_module = nullptr;
    bool com_initialized = false;
    bool active = false;
    bool pushed = false;
};

TsfInputContext::TsfInputContext(HWND hwnd, EventSink sink)
    : impl_(std::make_unique<Impl>()) {
    impl_->hwnd = hwnd;
    impl_->sink = std::move(sink);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // RPC_E_CHANGED_MODE is a failure: this context did not initialize COM,
    // so it must not CoUninitialize() later (0.7 audit F6).
    impl_->com_initialized = SUCCEEDED(com);
    if (!impl_->com_initialized) {
        return;
    }
    impl_->msctf_module = LoadLibraryW(L"msctf.dll");
    if (impl_->msctf_module == nullptr) {
        return;
    }
    const auto create_mgr = reinterpret_cast<CreateThreadMgrFn>(
        GetProcAddress(impl_->msctf_module, "TF_CreateThreadMgr"));
    if (create_mgr == nullptr || FAILED(create_mgr(&impl_->thread_mgr))) {
        return;
    }
    if (FAILED(impl_->thread_mgr->Activate(&impl_->client_id))) {
        return;
    }
    if (FAILED(impl_->thread_mgr->CreateDocumentMgr(&impl_->doc_mgr))) {
        return;
    }

    impl_->text_store_weak = impl_->store =
        new TextStore(hwnd, [this](const EventType type, std::string text) {
        Event event{};
        event.type = type;
        event.text = std::move(text);
        if (impl_->sink) {
            impl_->sink(event);
        }
    });
    if (FAILED(impl_->doc_mgr->CreateContext(
            impl_->client_id, 0, static_cast<ITextStoreACP*>(impl_->store),
            &impl_->context, &impl_->edit_cookie))) {
        impl_->store->Release();
        impl_->store = nullptr;
        impl_->text_store_weak = nullptr;
        return;
    }
    impl_->store->set_edit_cookie(impl_->edit_cookie);
    // Push the context onto the document manager stack. TSF calls
    // ITextStoreACP::AdviseSink from Push; without it the store never gets an
    // advise sink, no text service can lock it, and no composition can start
    // (0.7 audit F1 — this made the whole IME path dead).
    if (FAILED(impl_->doc_mgr->Push(impl_->context))) {
        impl_->store->Release();
        impl_->store = nullptr;
        impl_->text_store_weak = nullptr;
        return;
    }
    impl_->pushed = true;
    impl_->store->Release();  // context holds the only strong reference now
    impl_->store = nullptr;
}

TsfInputContext::~TsfInputContext() {
    // Tear down in reverse dependency order (0.7 audit F8): clear the
    // window association and focus, pop the context, then release context /
    // document manager before deactivating the thread manager.
    if (impl_->thread_mgr != nullptr && impl_->active) {
        impl_->thread_mgr->AssociateFocus(impl_->hwnd, nullptr, nullptr);
        impl_->thread_mgr->SetFocus(nullptr);
        impl_->active = false;
    }
    if (impl_->pushed && impl_->doc_mgr != nullptr) {
        impl_->doc_mgr->Pop(0);
        impl_->pushed = false;
    }
    impl_->text_store_weak = nullptr;  // invalid once context is released
    if (impl_->context != nullptr) {
        impl_->context->Release();
        impl_->context = nullptr;
    }
    if (impl_->doc_mgr != nullptr) {
        impl_->doc_mgr->Release();
        impl_->doc_mgr = nullptr;
    }
    if (impl_->thread_mgr != nullptr) {
        impl_->thread_mgr->Deactivate();
        impl_->thread_mgr->Release();
        impl_->thread_mgr = nullptr;
    }
    if (impl_->msctf_module != nullptr) {
        FreeLibrary(impl_->msctf_module);
    }
    if (impl_->com_initialized) {
        CoUninitialize();
    }
}

void TsfInputContext::sync_state(const std::string_view utf8_text,
                                 const Rect caret, const Rect text_bounds) noexcept {
    if (impl_->text_store_weak != nullptr) {
        impl_->text_store_weak->sync_state(utf8_text, caret, text_bounds);
    }
    // Tell TSF the caret/layout moved so candidate windows reposition (§8.3).
    if (impl_->context != nullptr) {
        ITfContextOwnerServices* services = nullptr;
        if (SUCCEEDED(impl_->context->QueryInterface(
                kIidTfContextOwnerServices,
                reinterpret_cast<void**>(&services)))) {
            services->OnLayoutChange();
            services->Release();
        }
    }
}

void TsfInputContext::on_focus(const bool focused) noexcept {
    if (impl_->thread_mgr == nullptr || impl_->doc_mgr == nullptr) {
        return;
    }
    if (focused) {
        impl_->thread_mgr->AssociateFocus(impl_->hwnd, impl_->doc_mgr, nullptr);
        impl_->thread_mgr->SetFocus(impl_->doc_mgr);
        impl_->active = true;
    } else if (impl_->active) {
        impl_->thread_mgr->SetFocus(nullptr);
        impl_->active = false;
    }
}

}  // namespace continui::detail
