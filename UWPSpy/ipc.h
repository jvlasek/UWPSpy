#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

namespace ipc {
namespace json = winrt::Windows::Data::Json;
inline void String(json::JsonObject const& o, wchar_t const* key, std::wstring_view value) {
    o.Insert(key, json::JsonValue::CreateStringValue(value));
}
inline json::JsonObject Error(std::wstring_view message) {
    json::JsonObject o; String(o, L"error", message); return o;
}
// One request is dispatched at a time. No XAML or dialog pointers cross threads.
struct Request {
    std::wstring input, output;
    winrt::handle done{CreateEvent(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<int> state{0}; // queued, running, canceled
    ULONGLONG deadline = GetTickCount64() + 15000;
};
class Server {
public:
    static constexpr UINT Message = WM_APP + 27;
    static std::shared_ptr<Server> Acquire();
    Server();
    ~Server();
    std::wstring Register(HWND window, DWORD tid);
    void Unregister(std::wstring const& tree);
    std::shared_ptr<Request> Take(std::wstring const& tree);
    void Publish(std::wstring const& tree, json::JsonObject const& event);
private:
    struct Tree { HWND window; DWORD tid; std::shared_ptr<Request> request; };
    void Run() noexcept;
    bool Transfer(HANDLE pipe, void* data, DWORD bytes, bool write);
    bool Await(HANDLE pipe, OVERLAPPED& op, DWORD timeout, DWORD* transferred);
    std::wstring Process(std::wstring const& input);
    std::mutex m_mutex;
    std::unordered_map<std::wstring, Tree> m_trees;
    std::deque<std::pair<uint64_t, std::wstring>> m_events;
    uint64_t m_nextTree = 0, m_sequence = 0;
    std::wstring m_name;
    winrt::handle m_stop{CreateEvent(nullptr, TRUE, FALSE, nullptr)};
    winrt::handle m_pipe;
    std::thread m_worker;
};
}
