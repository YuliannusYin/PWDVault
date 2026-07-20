// coding: utf-8
// =============================================================================
// Serializer.cpp
//
// PwdVault IPC 协议二进制序列化/反序列化实现。
//
// 所有字段按小端序写入（x86/Windows on ARM 均为小端，直接 memcpy）。
// 变长字段以 uint32_t 长度前缀打头；定长字段直接 memcpy。
// 复合结构按字段声明顺序逐字段写入，不写额外 tag。
// =============================================================================
#include "Serializer.h"

#include <cstring>
#include <utility>

namespace pwdvault::protocol {

// ---------------------------------------------------------------------------
// 内部辅助：Writer / Reader 与变长字段的读写函数
// ---------------------------------------------------------------------------
namespace {

/// 字节写入器：将定长/变长字段追加到内部 ByteVec。
class Writer {
public:
    void write_bytes(const void* data, size_t size) {
        const auto* p = static_cast<const std::byte*>(data);
        buffer_.insert(buffer_.end(), p, p + size);
    }
    void write_u16(uint16_t v) { write_bytes(&v, sizeof(v)); }
    void write_u32(uint32_t v) { write_bytes(&v, sizeof(v)); }
    void write_u64(uint64_t v) { write_bytes(&v, sizeof(v)); }
    void write_i64(int64_t v)  { write_bytes(&v, sizeof(v)); }
    void write_bool(bool v) {
        const uint8_t b = v ? 1u : 0u;
        write_bytes(&b, sizeof(b));
    }

    void write_string(const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        write_bytes(s.data(), s.size());
    }

    void write_byte_vec(const core::ByteVec& v) {
        write_u32(static_cast<uint32_t>(v.size()));
        if (!v.empty()) {
            write_bytes(v.data(), v.size());
        }
    }

    core::ByteVec take() { return std::move(buffer_); }

private:
    core::ByteVec buffer_;
};

/// 字节读取器：从 ByteSpan 按序读取字段，越界返回 false。
class Reader {
public:
    explicit Reader(core::ByteSpan data) : data_(data), pos_(0) {}

    bool read_bytes(void* out, size_t size) {
        if (pos_ + size > data_.size()) return false;
        std::memcpy(out, data_.data() + pos_, size);
        pos_ += size;
        return true;
    }
    bool read_u16(uint16_t& out) { return read_bytes(&out, sizeof(out)); }
    bool read_u32(uint32_t& out) { return read_bytes(&out, sizeof(out)); }
    bool read_u64(uint64_t& out) { return read_bytes(&out, sizeof(out)); }
    bool read_i64(int64_t& out)  { return read_bytes(&out, sizeof(out)); }
    bool read_bool(bool& out) {
        uint8_t b = 0;
        if (!read_bytes(&b, sizeof(b))) return false;
        out = (b != 0u);
        return true;
    }

    /// 读取字符串：先读 uint32_t 长度，再读对应字节数。
    /// 长度超过剩余字节时返回 false，避免恶意长度导致 bad_alloc。
    bool read_string(std::string& out) {
        uint32_t len = 0;
        if (!read_u32(len)) return false;
        if (len > remaining()) return false;
        out.resize(len);
        if (len > 0 && !read_bytes(out.data(), len)) return false;
        return true;
    }

    /// 读取 ByteVec：与 read_string 同策略。
    bool read_byte_vec(core::ByteVec& out) {
        uint32_t len = 0;
        if (!read_u32(len)) return false;
        if (len > remaining()) return false;
        out.resize(len);
        if (len > 0 && !read_bytes(out.data(), len)) return false;
        return true;
    }

    size_t remaining() const { return data_.size() - pos_; }

private:
    core::ByteSpan data_;
    size_t pos_;
};

core::Error make_error(const char* what) {
    return core::Error(core::ErrorCode::InvalidArgument, what);
}

void write_password_entry(Writer& w, const core::PasswordEntry& e) {
    w.write_i64(e.id);
    w.write_string(e.website);
    w.write_string(e.username);
    w.write_string(e.password);
    w.write_string(e.note);
    w.write_i64(e.created_at);
    w.write_i64(e.updated_at);
    w.write_byte_vec(e.iv);
    w.write_byte_vec(e.tag);
}

bool read_password_entry(Reader& r, core::PasswordEntry& out) {
    if (!r.read_i64(out.id)) return false;
    if (!r.read_string(out.website)) return false;
    if (!r.read_string(out.username)) return false;
    if (!r.read_string(out.password)) return false;
    if (!r.read_string(out.note)) return false;
    if (!r.read_i64(out.created_at)) return false;
    if (!r.read_i64(out.updated_at)) return false;
    if (!r.read_byte_vec(out.iv)) return false;
    if (!r.read_byte_vec(out.tag)) return false;
    return true;
}

void write_search_query(Writer& w, const core::SearchQuery& q) {
    w.write_string(q.text);
    w.write_u32(static_cast<uint32_t>(q.fields.size()));
    for (const auto& f : q.fields) {
        w.write_string(f);
    }
    w.write_bool(q.case_sensitive);
}

bool read_search_query(Reader& r, core::SearchQuery& out) {
    if (!r.read_string(out.text)) return false;
    uint32_t count = 0;
    if (!r.read_u32(count)) return false;
    if (count > r.remaining()) return false;  // 粗略上界保护
    out.fields.clear();
    out.fields.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::string s;
        if (!r.read_string(s)) return false;
        out.fields.push_back(std::move(s));
    }
    if (!r.read_bool(out.case_sensitive)) return false;
    return true;
}

void write_generator_options(Writer& w, const core::PasswordGeneratorOptions& o) {
    w.write_u64(static_cast<uint64_t>(o.length));
    w.write_bool(o.use_uppercase);
    w.write_bool(o.use_lowercase);
    w.write_bool(o.use_digits);
    w.write_bool(o.use_symbols);
    w.write_string(o.custom_chars);
    w.write_bool(o.exclude_ambiguous);
}

bool read_generator_options(Reader& r, core::PasswordGeneratorOptions& out) {
    uint64_t len = 0;
    if (!r.read_u64(len)) return false;
    out.length = static_cast<size_t>(len);
    if (!r.read_bool(out.use_uppercase)) return false;
    if (!r.read_bool(out.use_lowercase)) return false;
    if (!r.read_bool(out.use_digits)) return false;
    if (!r.read_bool(out.use_symbols)) return false;
    if (!r.read_string(out.custom_chars)) return false;
    if (!r.read_bool(out.exclude_ambiguous)) return false;
    return true;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// command_name（在 Commands.h 中声明）
// ---------------------------------------------------------------------------
std::string_view command_name(CommandId cmd) noexcept {
    switch (cmd) {
        case CommandId::Ping:             return "Ping";
        case CommandId::Shutdown:         return "Shutdown";
        case CommandId::Login:            return "Login";
        case CommandId::Unlock:           return "Unlock";
        case CommandId::Lock:             return "Lock";
        case CommandId::AddEntry:         return "AddEntry";
        case CommandId::UpdateEntry:      return "UpdateEntry";
        case CommandId::RemoveEntry:      return "RemoveEntry";
        case CommandId::GetEntry:         return "GetEntry";
        case CommandId::SearchEntries:    return "SearchEntries";
        case CommandId::ListEntries:      return "ListEntries";
        case CommandId::GeneratePassword: return "GeneratePassword";
        case CommandId::EstimateStrength: return "EstimateStrength";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// 基础类型特化
// ---------------------------------------------------------------------------

template <> core::ByteVec serialize<uint16_t>(const uint16_t& v) {
    Writer w; w.write_u16(v); return w.take();
}
template <> core::Result<uint16_t> deserialize<uint16_t>(core::ByteSpan data) {
    Reader r(data); uint16_t v = 0;
    if (!r.read_u16(v)) return core::Result<uint16_t>::Err(make_error("deserialize<uint16_t>: data too short"));
    return core::Result<uint16_t>::Ok(v);
}

template <> core::ByteVec serialize<uint32_t>(const uint32_t& v) {
    Writer w; w.write_u32(v); return w.take();
}
template <> core::Result<uint32_t> deserialize<uint32_t>(core::ByteSpan data) {
    Reader r(data); uint32_t v = 0;
    if (!r.read_u32(v)) return core::Result<uint32_t>::Err(make_error("deserialize<uint32_t>: data too short"));
    return core::Result<uint32_t>::Ok(v);
}

template <> core::ByteVec serialize<uint64_t>(const uint64_t& v) {
    Writer w; w.write_u64(v); return w.take();
}
template <> core::Result<uint64_t> deserialize<uint64_t>(core::ByteSpan data) {
    Reader r(data); uint64_t v = 0;
    if (!r.read_u64(v)) return core::Result<uint64_t>::Err(make_error("deserialize<uint64_t>: data too short"));
    return core::Result<uint64_t>::Ok(v);
}

template <> core::ByteVec serialize<int64_t>(const int64_t& v) {
    Writer w; w.write_i64(v); return w.take();
}
template <> core::Result<int64_t> deserialize<int64_t>(core::ByteSpan data) {
    Reader r(data); int64_t v = 0;
    if (!r.read_i64(v)) return core::Result<int64_t>::Err(make_error("deserialize<int64_t>: data too short"));
    return core::Result<int64_t>::Ok(v);
}

template <> core::ByteVec serialize<bool>(const bool& v) {
    Writer w; w.write_bool(v); return w.take();
}
template <> core::Result<bool> deserialize<bool>(core::ByteSpan data) {
    Reader r(data); bool v = false;
    if (!r.read_bool(v)) return core::Result<bool>::Err(make_error("deserialize<bool>: data too short"));
    return core::Result<bool>::Ok(v);
}

template <> core::ByteVec serialize<std::string>(const std::string& v) {
    Writer w; w.write_string(v); return w.take();
}
template <> core::Result<std::string> deserialize<std::string>(core::ByteSpan data) {
    Reader r(data); std::string v;
    if (!r.read_string(v)) return core::Result<std::string>::Err(make_error("deserialize<std::string>: malformed"));
    return core::Result<std::string>::Ok(std::move(v));
}

template <> core::ByteVec serialize<core::ByteVec>(const core::ByteVec& v) {
    Writer w; w.write_byte_vec(v); return w.take();
}
template <> core::Result<core::ByteVec> deserialize<core::ByteVec>(core::ByteSpan data) {
    Reader r(data); core::ByteVec v;
    if (!r.read_byte_vec(v)) return core::Result<core::ByteVec>::Err(make_error("deserialize<ByteVec>: malformed"));
    return core::Result<core::ByteVec>::Ok(std::move(v));
}

// ---------------------------------------------------------------------------
// core 类型特化
// ---------------------------------------------------------------------------

template <> core::ByteVec serialize<core::ErrorCode>(const core::ErrorCode& v) {
    Writer w; w.write_u32(static_cast<uint32_t>(v)); return w.take();
}
template <> core::Result<core::ErrorCode> deserialize<core::ErrorCode>(core::ByteSpan data) {
    Reader r(data); uint32_t v = 0;
    if (!r.read_u32(v)) return core::Result<core::ErrorCode>::Err(make_error("deserialize<ErrorCode>: data too short"));
    return core::Result<core::ErrorCode>::Ok(static_cast<core::ErrorCode>(v));
}

template <> core::ByteVec serialize<core::PasswordEntry>(const core::PasswordEntry& v) {
    Writer w; write_password_entry(w, v); return w.take();
}
template <> core::Result<core::PasswordEntry> deserialize<core::PasswordEntry>(core::ByteSpan data) {
    Reader r(data); core::PasswordEntry v;
    if (!read_password_entry(r, v)) return core::Result<core::PasswordEntry>::Err(make_error("deserialize<PasswordEntry>: malformed"));
    return core::Result<core::PasswordEntry>::Ok(std::move(v));
}

template <> core::ByteVec serialize<core::SearchQuery>(const core::SearchQuery& v) {
    Writer w; write_search_query(w, v); return w.take();
}
template <> core::Result<core::SearchQuery> deserialize<core::SearchQuery>(core::ByteSpan data) {
    Reader r(data); core::SearchQuery v;
    if (!read_search_query(r, v)) return core::Result<core::SearchQuery>::Err(make_error("deserialize<SearchQuery>: malformed"));
    return core::Result<core::SearchQuery>::Ok(std::move(v));
}

template <> core::ByteVec serialize<core::PasswordGeneratorOptions>(const core::PasswordGeneratorOptions& v) {
    Writer w; write_generator_options(w, v); return w.take();
}
template <> core::Result<core::PasswordGeneratorOptions> deserialize<core::PasswordGeneratorOptions>(core::ByteSpan data) {
    Reader r(data); core::PasswordGeneratorOptions v;
    if (!read_generator_options(r, v)) return core::Result<core::PasswordGeneratorOptions>::Err(make_error("deserialize<PasswordGeneratorOptions>: malformed"));
    return core::Result<core::PasswordGeneratorOptions>::Ok(std::move(v));
}

// ---------------------------------------------------------------------------
// 请求消息特化
// ---------------------------------------------------------------------------

template <> core::ByteVec serialize<PingRequest>(const PingRequest&) {
    return {};
}
template <> core::Result<PingRequest> deserialize<PingRequest>(core::ByteSpan) {
    return core::Result<PingRequest>::Ok(PingRequest{});
}

template <> core::ByteVec serialize<ShutdownRequest>(const ShutdownRequest&) {
    return {};
}
template <> core::Result<ShutdownRequest> deserialize<ShutdownRequest>(core::ByteSpan) {
    return core::Result<ShutdownRequest>::Ok(ShutdownRequest{});
}

template <> core::ByteVec serialize<LoginRequest>(const LoginRequest& v) {
    Writer w;
    w.write_string(v.password);
    w.write_bool(v.is_first_time);
    return w.take();
}
template <> core::Result<LoginRequest> deserialize<LoginRequest>(core::ByteSpan data) {
    Reader r(data); LoginRequest v;
    if (!r.read_string(v.password)) return core::Result<LoginRequest>::Err(make_error("deserialize<LoginRequest>: malformed"));
    if (!r.read_bool(v.is_first_time)) return core::Result<LoginRequest>::Err(make_error("deserialize<LoginRequest>: malformed"));
    return core::Result<LoginRequest>::Ok(std::move(v));
}

template <> core::ByteVec serialize<UnlockRequest>(const UnlockRequest& v) {
    Writer w;
    w.write_string(v.password);
    return w.take();
}
template <> core::Result<UnlockRequest> deserialize<UnlockRequest>(core::ByteSpan data) {
    Reader r(data); UnlockRequest v;
    if (!r.read_string(v.password)) return core::Result<UnlockRequest>::Err(make_error("deserialize<UnlockRequest>: malformed"));
    return core::Result<UnlockRequest>::Ok(std::move(v));
}

template <> core::ByteVec serialize<LockRequest>(const LockRequest&) {
    return {};
}
template <> core::Result<LockRequest> deserialize<LockRequest>(core::ByteSpan) {
    return core::Result<LockRequest>::Ok(LockRequest{});
}

template <> core::ByteVec serialize<AddEntryRequest>(const AddEntryRequest& v) {
    Writer w; write_password_entry(w, v.entry); return w.take();
}
template <> core::Result<AddEntryRequest> deserialize<AddEntryRequest>(core::ByteSpan data) {
    Reader r(data); AddEntryRequest v;
    if (!read_password_entry(r, v.entry)) return core::Result<AddEntryRequest>::Err(make_error("deserialize<AddEntryRequest>: malformed"));
    return core::Result<AddEntryRequest>::Ok(std::move(v));
}

template <> core::ByteVec serialize<UpdateEntryRequest>(const UpdateEntryRequest& v) {
    Writer w; write_password_entry(w, v.entry); return w.take();
}
template <> core::Result<UpdateEntryRequest> deserialize<UpdateEntryRequest>(core::ByteSpan data) {
    Reader r(data); UpdateEntryRequest v;
    if (!read_password_entry(r, v.entry)) return core::Result<UpdateEntryRequest>::Err(make_error("deserialize<UpdateEntryRequest>: malformed"));
    return core::Result<UpdateEntryRequest>::Ok(std::move(v));
}

template <> core::ByteVec serialize<RemoveEntryRequest>(const RemoveEntryRequest& v) {
    Writer w; w.write_i64(v.id); return w.take();
}
template <> core::Result<RemoveEntryRequest> deserialize<RemoveEntryRequest>(core::ByteSpan data) {
    Reader r(data); RemoveEntryRequest v;
    if (!r.read_i64(v.id)) return core::Result<RemoveEntryRequest>::Err(make_error("deserialize<RemoveEntryRequest>: data too short"));
    return core::Result<RemoveEntryRequest>::Ok(v);
}

template <> core::ByteVec serialize<GetEntryRequest>(const GetEntryRequest& v) {
    Writer w; w.write_i64(v.id); return w.take();
}
template <> core::Result<GetEntryRequest> deserialize<GetEntryRequest>(core::ByteSpan data) {
    Reader r(data); GetEntryRequest v;
    if (!r.read_i64(v.id)) return core::Result<GetEntryRequest>::Err(make_error("deserialize<GetEntryRequest>: data too short"));
    return core::Result<GetEntryRequest>::Ok(v);
}

template <> core::ByteVec serialize<SearchEntriesRequest>(const SearchEntriesRequest& v) {
    Writer w; write_search_query(w, v.query); return w.take();
}
template <> core::Result<SearchEntriesRequest> deserialize<SearchEntriesRequest>(core::ByteSpan data) {
    Reader r(data); SearchEntriesRequest v;
    if (!read_search_query(r, v.query)) return core::Result<SearchEntriesRequest>::Err(make_error("deserialize<SearchEntriesRequest>: malformed"));
    return core::Result<SearchEntriesRequest>::Ok(std::move(v));
}

template <> core::ByteVec serialize<ListEntriesRequest>(const ListEntriesRequest&) {
    return {};
}
template <> core::Result<ListEntriesRequest> deserialize<ListEntriesRequest>(core::ByteSpan) {
    return core::Result<ListEntriesRequest>::Ok(ListEntriesRequest{});
}

template <> core::ByteVec serialize<GeneratePasswordRequest>(const GeneratePasswordRequest& v) {
    Writer w; write_generator_options(w, v.options); return w.take();
}
template <> core::Result<GeneratePasswordRequest> deserialize<GeneratePasswordRequest>(core::ByteSpan data) {
    Reader r(data); GeneratePasswordRequest v;
    if (!read_generator_options(r, v.options)) return core::Result<GeneratePasswordRequest>::Err(make_error("deserialize<GeneratePasswordRequest>: malformed"));
    return core::Result<GeneratePasswordRequest>::Ok(std::move(v));
}

template <> core::ByteVec serialize<EstimateStrengthRequest>(const EstimateStrengthRequest& v) {
    Writer w; w.write_string(v.password); return w.take();
}
template <> core::Result<EstimateStrengthRequest> deserialize<EstimateStrengthRequest>(core::ByteSpan data) {
    Reader r(data); EstimateStrengthRequest v;
    if (!r.read_string(v.password)) return core::Result<EstimateStrengthRequest>::Err(make_error("deserialize<EstimateStrengthRequest>: malformed"));
    return core::Result<EstimateStrengthRequest>::Ok(std::move(v));
}

// ---------------------------------------------------------------------------
// 响应消息特化
// ---------------------------------------------------------------------------

template <> core::ByteVec serialize<PingResponse>(const PingResponse& v) {
    Writer w; w.write_u64(v.server_timestamp); return w.take();
}
template <> core::Result<PingResponse> deserialize<PingResponse>(core::ByteSpan data) {
    Reader r(data); PingResponse v;
    if (!r.read_u64(v.server_timestamp)) return core::Result<PingResponse>::Err(make_error("deserialize<PingResponse>: data too short"));
    return core::Result<PingResponse>::Ok(v);
}

template <> core::ByteVec serialize<ShutdownResponse>(const ShutdownResponse&) {
    return {};
}
template <> core::Result<ShutdownResponse> deserialize<ShutdownResponse>(core::ByteSpan) {
    return core::Result<ShutdownResponse>::Ok(ShutdownResponse{});
}

template <> core::ByteVec serialize<LoginResponse>(const LoginResponse& v) {
    Writer w;
    w.write_bool(v.success);
    w.write_string(v.error_message);
    return w.take();
}
template <> core::Result<LoginResponse> deserialize<LoginResponse>(core::ByteSpan data) {
    Reader r(data); LoginResponse v;
    if (!r.read_bool(v.success)) return core::Result<LoginResponse>::Err(make_error("deserialize<LoginResponse>: malformed"));
    if (!r.read_string(v.error_message)) return core::Result<LoginResponse>::Err(make_error("deserialize<LoginResponse>: malformed"));
    return core::Result<LoginResponse>::Ok(std::move(v));
}

template <> core::ByteVec serialize<UnlockResponse>(const UnlockResponse& v) {
    Writer w;
    w.write_bool(v.success);
    w.write_string(v.error_message);
    return w.take();
}
template <> core::Result<UnlockResponse> deserialize<UnlockResponse>(core::ByteSpan data) {
    Reader r(data); UnlockResponse v;
    if (!r.read_bool(v.success)) return core::Result<UnlockResponse>::Err(make_error("deserialize<UnlockResponse>: malformed"));
    if (!r.read_string(v.error_message)) return core::Result<UnlockResponse>::Err(make_error("deserialize<UnlockResponse>: malformed"));
    return core::Result<UnlockResponse>::Ok(std::move(v));
}

template <> core::ByteVec serialize<LockResponse>(const LockResponse&) {
    return {};
}
template <> core::Result<LockResponse> deserialize<LockResponse>(core::ByteSpan) {
    return core::Result<LockResponse>::Ok(LockResponse{});
}

template <> core::ByteVec serialize<AddEntryResponse>(const AddEntryResponse& v) {
    Writer w; write_password_entry(w, v.entry); return w.take();
}
template <> core::Result<AddEntryResponse> deserialize<AddEntryResponse>(core::ByteSpan data) {
    Reader r(data); AddEntryResponse v;
    if (!read_password_entry(r, v.entry)) return core::Result<AddEntryResponse>::Err(make_error("deserialize<AddEntryResponse>: malformed"));
    return core::Result<AddEntryResponse>::Ok(std::move(v));
}

template <> core::ByteVec serialize<UpdateEntryResponse>(const UpdateEntryResponse& v) {
    Writer w; write_password_entry(w, v.entry); return w.take();
}
template <> core::Result<UpdateEntryResponse> deserialize<UpdateEntryResponse>(core::ByteSpan data) {
    Reader r(data); UpdateEntryResponse v;
    if (!read_password_entry(r, v.entry)) return core::Result<UpdateEntryResponse>::Err(make_error("deserialize<UpdateEntryResponse>: malformed"));
    return core::Result<UpdateEntryResponse>::Ok(std::move(v));
}

template <> core::ByteVec serialize<RemoveEntryResponse>(const RemoveEntryResponse&) {
    return {};
}
template <> core::Result<RemoveEntryResponse> deserialize<RemoveEntryResponse>(core::ByteSpan) {
    return core::Result<RemoveEntryResponse>::Ok(RemoveEntryResponse{});
}

template <> core::ByteVec serialize<GetEntryResponse>(const GetEntryResponse& v) {
    Writer w; write_password_entry(w, v.entry); return w.take();
}
template <> core::Result<GetEntryResponse> deserialize<GetEntryResponse>(core::ByteSpan data) {
    Reader r(data); GetEntryResponse v;
    if (!read_password_entry(r, v.entry)) return core::Result<GetEntryResponse>::Err(make_error("deserialize<GetEntryResponse>: malformed"));
    return core::Result<GetEntryResponse>::Ok(std::move(v));
}

namespace {

/// 写入 vector<T>：长度前缀 + 逐元素序列化。
void write_password_entry_vector(Writer& w, const std::vector<core::PasswordEntry>& v) {
    w.write_u32(static_cast<uint32_t>(v.size()));
    for (const auto& e : v) {
        write_password_entry(w, e);
    }
}

bool read_password_entry_vector(Reader& r, std::vector<core::PasswordEntry>& out) {
    uint32_t count = 0;
    if (!r.read_u32(count)) return false;
    if (count > r.remaining()) return false;  // 粗略上界保护
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        core::PasswordEntry e;
        if (!read_password_entry(r, e)) return false;
        out.push_back(std::move(e));
    }
    return true;
}

}  // anonymous namespace

template <> core::ByteVec serialize<SearchEntriesResponse>(const SearchEntriesResponse& v) {
    Writer w; write_password_entry_vector(w, v.entries); return w.take();
}
template <> core::Result<SearchEntriesResponse> deserialize<SearchEntriesResponse>(core::ByteSpan data) {
    Reader r(data); SearchEntriesResponse v;
    if (!read_password_entry_vector(r, v.entries)) return core::Result<SearchEntriesResponse>::Err(make_error("deserialize<SearchEntriesResponse>: malformed"));
    return core::Result<SearchEntriesResponse>::Ok(std::move(v));
}

template <> core::ByteVec serialize<ListEntriesResponse>(const ListEntriesResponse& v) {
    Writer w; write_password_entry_vector(w, v.entries); return w.take();
}
template <> core::Result<ListEntriesResponse> deserialize<ListEntriesResponse>(core::ByteSpan data) {
    Reader r(data); ListEntriesResponse v;
    if (!read_password_entry_vector(r, v.entries)) return core::Result<ListEntriesResponse>::Err(make_error("deserialize<ListEntriesResponse>: malformed"));
    return core::Result<ListEntriesResponse>::Ok(std::move(v));
}

template <> core::ByteVec serialize<GeneratePasswordResponse>(const GeneratePasswordResponse& v) {
    Writer w; w.write_string(v.password); return w.take();
}
template <> core::Result<GeneratePasswordResponse> deserialize<GeneratePasswordResponse>(core::ByteSpan data) {
    Reader r(data); GeneratePasswordResponse v;
    if (!r.read_string(v.password)) return core::Result<GeneratePasswordResponse>::Err(make_error("deserialize<GeneratePasswordResponse>: malformed"));
    return core::Result<GeneratePasswordResponse>::Ok(std::move(v));
}

template <> core::ByteVec serialize<EstimateStrengthResponse>(const EstimateStrengthResponse& v) {
    Writer w; w.write_i64(v.strength_bits); return w.take();
}
template <> core::Result<EstimateStrengthResponse> deserialize<EstimateStrengthResponse>(core::ByteSpan data) {
    Reader r(data); EstimateStrengthResponse v;
    if (!r.read_i64(v.strength_bits)) return core::Result<EstimateStrengthResponse>::Err(make_error("deserialize<EstimateStrengthResponse>: data too short"));
    return core::Result<EstimateStrengthResponse>::Ok(v);
}

template <> core::ByteVec serialize<ErrorResponse>(const ErrorResponse& v) {
    Writer w;
    w.write_u32(static_cast<uint32_t>(v.code));
    w.write_string(v.message);
    return w.take();
}
template <> core::Result<ErrorResponse> deserialize<ErrorResponse>(core::ByteSpan data) {
    Reader r(data); ErrorResponse v;
    uint32_t code = 0;
    if (!r.read_u32(code)) return core::Result<ErrorResponse>::Err(make_error("deserialize<ErrorResponse>: malformed"));
    v.code = static_cast<core::ErrorCode>(code);
    if (!r.read_string(v.message)) return core::Result<ErrorResponse>::Err(make_error("deserialize<ErrorResponse>: malformed"));
    return core::Result<ErrorResponse>::Ok(std::move(v));
}

// ---------------------------------------------------------------------------
// 消息帧封包/解包
// ---------------------------------------------------------------------------

core::ByteVec pack_message(CommandId cmd, uint32_t request_id, core::ByteSpan payload) {
    MessageHeader h;
    h.magic = kMagic;
    h.version = kProtocolVersion;
    h.command = cmd;
    h.request_id = request_id;
    h.payload_size = static_cast<uint32_t>(payload.size());

    core::ByteVec out;
    out.reserve(sizeof(h) + payload.size());
    const auto* hp = reinterpret_cast<const std::byte*>(&h);
    out.insert(out.end(), hp, hp + sizeof(h));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

core::Result<std::pair<MessageHeader, size_t>> parse_header(core::ByteSpan data) {
    if (data.size() < sizeof(MessageHeader)) {
        return core::Result<std::pair<MessageHeader, size_t>>::Err(
            core::Error(core::ErrorCode::IpcError,
                        "parse_header: data too short for header (need 16 bytes)"));
    }
    MessageHeader h;
    std::memcpy(&h, data.data(), sizeof(h));
    if (h.magic != kMagic) {
        return core::Result<std::pair<MessageHeader, size_t>>::Err(
            core::Error(core::ErrorCode::IpcError,
                        "parse_header: magic mismatch"));
    }
    return core::Result<std::pair<MessageHeader, size_t>>::Ok(
        std::make_pair(h, sizeof(MessageHeader)));
}

}  // namespace pwdvault::protocol
