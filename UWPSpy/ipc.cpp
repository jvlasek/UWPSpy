#include <windows.h>
#include <winrt/base.h>
#include <format>
#include <unordered_map>
#include <vector>
#include "ipc.h"
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")
namespace ipc {
std::shared_ptr<Server> Server::Acquire() {
    static std::mutex mutex;
    static std::weak_ptr<Server> instance;
    std::lock_guard lock(mutex);
    auto server = instance.lock();
    if (!server) { server = std::make_shared<Server>(); instance = server; }
    return server;
}
Server::Server() {
    winrt::check_bool(bool(m_stop));
    GUID guid; winrt::check_hresult(CoCreateGuid(&guid));
    wchar_t id[40]; StringFromGUID2(guid, id, ARRAYSIZE(id));
    m_name = std::format(L"\\\\.\\pipe\\UWPSpy-{}-{}", GetCurrentProcessId(), id);
    winrt::handle token;
    winrt::check_bool(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()));
    DWORD size = 0; GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<BYTE> storage(size);
    winrt::check_bool(GetTokenInformation(token.get(), TokenUser, storage.data(), size, &size));
    LPWSTR sid = nullptr;
    winrt::check_bool(ConvertSidToStringSid(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid));
    std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    winrt::check_bool(ConvertStringSecurityDescriptorToSecurityDescriptor(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    m_pipe.attach(CreateNamedPipe(m_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
        PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, &attributes));
    const DWORD error = GetLastError();
    LocalFree(descriptor);
    if (!m_pipe) winrt::throw_hresult(HRESULT_FROM_WIN32(error));
    m_worker = std::thread([this] { Run(); });
}
Server::~Server() {
    SetEvent(m_stop.get());
    if (m_worker.joinable()) m_worker.join();
}
std::wstring Server::Register(HWND window, DWORD tid) {
    std::lock_guard lock(m_mutex);
    auto id = std::to_wstring(++m_nextTree);
    m_trees.emplace(id, Tree{window, tid, {}});
    return id;
}
void Server::Unregister(std::wstring const& tree) {
    std::lock_guard lock(m_mutex);
    auto it = m_trees.find(tree);
    if (it != m_trees.end()) {
        if (auto request = it->second.request) {
            request->state = 2;
            request->output = L"{\"error\":\"inspection window closed\"}";
            SetEvent(request->done.get());
        }
        m_trees.erase(it);
    }
}
std::shared_ptr<Request> Server::Take(std::wstring const& tree) {
    std::lock_guard lock(m_mutex);
    auto it = m_trees.find(tree);
    if (it == m_trees.end()) return {};
    auto request = std::exchange(it->second.request, {});
    if (!request) return {};
    int expected = 0;
    if (GetTickCount64() > request->deadline || !request->state.compare_exchange_strong(expected, 1)) return {};
    return request;
}
void Server::Publish(std::wstring const& tree, json::JsonObject const& event) {
    std::lock_guard lock(m_mutex);
    String(event, L"tree", tree);
    String(event, L"sequence", std::to_wstring(++m_sequence));
    m_events.emplace_back(m_sequence, std::wstring(event.Stringify()));
    if (m_events.size() > 256) m_events.pop_front();
}
bool Server::Await(HANDLE pipe, OVERLAPPED& op, DWORD timeout, DWORD* transferred) {
    HANDLE waits[]{m_stop.get(), op.hEvent};
    if (WaitForMultipleObjects(2, waits, FALSE, timeout) != WAIT_OBJECT_0 + 1) {
        CancelIoEx(pipe, &op);
        GetOverlappedResult(pipe, &op, transferred, TRUE); // retire OVERLAPPED before its stack unwinds
        return false;
    }
    return GetOverlappedResult(pipe, &op, transferred, FALSE) != FALSE;
}
bool Server::Transfer(HANDLE pipe, void* data, DWORD bytes, bool write) {
    winrt::handle event(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    if (!event) return false;
    auto cursor = static_cast<BYTE*>(data);
    while (bytes) {
        if (WaitForSingleObject(m_stop.get(), 0) == WAIT_OBJECT_0) return false;
        OVERLAPPED op{}; op.hEvent = event.get(); ResetEvent(event.get());
        DWORD count = 0;
        BOOL ok = write ? WriteFile(pipe, cursor, bytes, &count, &op) : ReadFile(pipe, cursor, bytes, &count, &op);
        if (!ok && (GetLastError() != ERROR_IO_PENDING || !Await(pipe, op, 15000, &count))) return false;
        if (!count) return false;
        cursor += count; bytes -= count;
    }
    return true;
}
std::wstring Server::Process(std::wstring const& input) {
    auto command = json::JsonObject::Parse(input);
    auto op = command.GetNamedString(L"op");
    json::JsonObject result;
    String(result, L"request_id", command.GetNamedString(L"request_id", L""));
    String(result, L"endpoint", m_name);
    if (op == L"trees") {
        json::JsonArray trees;
        std::lock_guard lock(m_mutex);
        for (auto const& [id, tree] : m_trees) {
            json::JsonObject entry; String(entry, L"tree", id);
            entry.Insert(L"tid", json::JsonValue::CreateNumberValue(tree.tid));
            trees.Append(entry);
        }
        result.Insert(L"trees", trees);
        result.Insert(L"pid", json::JsonValue::CreateNumberValue(GetCurrentProcessId()));
        result.Insert(L"protocol", json::JsonValue::CreateNumberValue(1));
    } else if (op == L"events") {
        auto after = std::stoull(std::wstring(command.GetNamedString(L"after", L"0")));
        json::JsonArray events;
        std::lock_guard lock(m_mutex);
        for (auto const& [seq, text] : m_events) if (seq > after) events.Append(json::JsonObject::Parse(text));
        result.Insert(L"events", events);
        String(result, L"cursor", std::to_wstring(m_sequence));
        result.Insert(L"gap", json::JsonValue::CreateBooleanValue(!m_events.empty() && after < m_events.front().first - 1));
    } else {
        auto request = std::make_shared<Request>();
        if (!request->done) winrt::throw_last_error();
        request->input = input;
        std::wstring treeId(command.GetNamedString(L"tree"));
        {
            std::lock_guard lock(m_mutex);
            auto it = m_trees.find(treeId);
            if (it == m_trees.end()) return Error(L"unknown tree").Stringify().c_str();
            it->second.request = request;
            if (!PostMessage(it->second.window, Message, 0, 0)) {
                it->second.request.reset();
                winrt::throw_last_error();
            }
        }
        HANDLE waits[]{m_stop.get(), request->done.get()};
        if (WaitForMultipleObjects(2, waits, FALSE, 15000) != WAIT_OBJECT_0 + 1) {
            request->state = 2;
            return Error(L"UI request timed out; a running operation may still finish; do not blindly retry mutations").Stringify().c_str();
        }
        result.Insert(L"result", json::JsonObject::Parse(request->output));
    }
    return std::wstring(result.Stringify());
}
void Server::Run() noexcept {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        while (WaitForSingleObject(m_stop.get(), 0) != WAIT_OBJECT_0) {
            winrt::handle event(CreateEvent(nullptr, TRUE, FALSE, nullptr));
            if (!event) break;
            OVERLAPPED op{}; op.hEvent = event.get();
            DWORD ignored = 0;
            BOOL connected = ConnectNamedPipe(m_pipe.get(), &op);
            if (!connected) {
                DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING) connected = Await(m_pipe.get(), op, INFINITE, &ignored);
                else connected = error == ERROR_PIPE_CONNECTED;
            }
            if (!connected) break;
            // Framed request/response; no GUI-thread pipe I/O, including for watch events.
            DWORD size = 0;
            if (Transfer(m_pipe.get(), &size, sizeof(size), false) && size && size <= 65536) {
                std::string bytes(size, '\0');
                if (Transfer(m_pipe.get(), bytes.data(), size, false)) {
                    std::string output;
                    try { output = winrt::to_string(Process(std::wstring(winrt::to_hstring(bytes)))); }
                    catch (...) { output = winrt::to_string(Error(L"invalid request or inspection error").Stringify()); }
                    if (output.size() > 16 * 1024 * 1024) output = "{\"error\":\"response too large\"}";
                    size = static_cast<DWORD>(output.size());
                    if (Transfer(m_pipe.get(), &size, sizeof(size), true) && Transfer(m_pipe.get(), output.data(), size, true)) {
                        // Explicit client ACK avoids FlushFileBuffers, which can block shutdown.
                        BYTE ack; Transfer(m_pipe.get(), &ack, 1, false);
                    }
                }
            }
            DisconnectNamedPipe(m_pipe.get());
        }
        winrt::uninit_apartment();
    } catch (...) { /* Never unwind into Explorer. Existing inspection UI remains available. */ }
}
}
