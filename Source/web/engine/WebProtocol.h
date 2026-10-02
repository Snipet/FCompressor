#pragma once

// WebProtocol: the byte protocol between the editor (the page's main thread) and the engine module in the AudioWorklet
// (ADR-93). It is defined ONCE, here, in C++: both wasm modules include this header, and JavaScript only moves bytes
// (a MessagePort carries an ArrayBuffer from one module's memory to the other's; the one field a script reads is
// Header::bytes, the u32 at offset 8, to know how much to copy).
//
// Every message is one fixed-layout record: little-endian, 4-byte fields, no pointers, no padding, a versioned header
// first. A receiver checks the magic, the version, the kind and the byte count before it reads anything else, and a
// record that fails a check changes nothing.
//
//   to the engine (fcmp_web_post):
//     Params   the 30 plain parameter values in Pid order plus a snap flag. The engine resolves them exactly as the
//              plugin's processor does per block; a changed quality or lookahead budget reconfigures the engine inside
//              the handler, between render quanta. snap != 0: the values apply at once (a state load, a preset), else
//              they ramp (an edit).
//     Attach   the editor is listening (attached != 0) or has gone: telemetry is published only while it listens.
//     Reset    clear the engine's state (transport restart, a new source).
//     Pull     ask for a Reply. Sent once per drawn frame; the worklet returns the reply's bytes in the same buffer.
//   from the engine (the reply buffer, fcmp_web_reply):
//     Reply    a ReplyHead and the columns: the latest UiFrame, the HistoryRing columns published since the last reply (at most kReplyMaxColumns,
//              oldest first; the rest follow in the next reply), flags (a gap in the columns, the silence gate), and
//              the latency in samples. `bytes` counts the header, the fixed part and the columns delivered, so a reply
//              is copied as its first h.bytes bytes.
//
// The telemetry records are fcdsp's own (UiFrame 288 bytes, HistoryColumn 32 bytes: trivially copyable, pointer-free,
// the same layout on every target; their headers assert it).
//
// Portable C++ over fcdsp alone (lint web.engine): no JUCE, no Emscripten header.

#include "fcdsp/params/Pid.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace fcmp::web
{
    static_assert(std::endian::native == std::endian::little,
                  "WebProtocol records are little-endian and are copied as they lie in memory");
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE-754 binary32 parameter values");

    inline constexpr std::uint32_t kMagic = 0x50574346u;           // the bytes 'F' 'C' 'W' 'P'
    inline constexpr std::uint16_t kVersion = 1;                   // any layout or meaning change raises it

    enum class Kind : std::uint16_t
    {
        params = 1,
        attach = 2,
        reset = 3,
        pull = 4,
        reply = 0x8000,
    };

    struct Header                                                  // 16 bytes
    {
        std::uint32_t magic;                                       // kMagic
        std::uint16_t version;                                     // kVersion
        std::uint16_t kind;                                        // Kind
        std::uint32_t bytes;                                       // the whole record, this header included
        std::uint32_t tag;                                         // the sender's own; a Reply echoes its Pull's
    };

    inline constexpr std::size_t kParamCount = fcdsp::kNumParams;  // 30 (Pid.h)

    struct ParamsMsg                                               // 140 bytes
    {
        Header        h;
        float         plain[kParamCount];                          // plain values, Pid order
        std::uint32_t snap;                                        // != 0: apply without ramps
    };

    struct AttachMsg                                               // 20 bytes
    {
        Header        h;
        std::uint32_t attached;                                    // != 0: the editor is listening
    };

    struct ResetMsg                                                // 16 bytes
    {
        Header h;
    };

    struct PullMsg                                                 // 16 bytes
    {
        Header h;
    };

    enum ReplyFlag : std::uint32_t
    {
        kReplyGap        = 1u << 0,    // columns were lost before the first one delivered (the ring lapped the reader)
        kReplyMore       = 1u << 1,    // more columns are waiting than one reply holds: pull again
        kReplyFrame      = 1u << 2,    // `frame` is a consistent UiFrame (publishCount == 0: nothing published yet)
        kReplyGated      = 1u << 3,    // the silence gate is closed: the engine is idle and the frame is not moving
        kReplyConfigured = 1u << 4,    // the engine has been configured (latencySamples is the running engine's)
        kReplyAttached   = 1u << 5,    // the engine publishes telemetry (an Attach arrived)
    };

    inline constexpr std::uint32_t kReplyMaxColumns = 512;         // 0.5 s of 1 ms columns per reply

    struct ReplyHead                                               // 320 bytes: the fixed part of a reply
    {
        Header               h;                                    // kind = reply; bytes = 320 + 32 * columnCount
        std::uint32_t        flags;                                // ReplyFlag
        std::uint32_t        latencySamples;
        std::uint32_t        columnCount;                          // <= kReplyMaxColumns
        std::uint32_t        firstColumn;                          // low 32 bits of the first delivered column's index
        fcdsp::UiFrame       frame;
    };

    struct Reply                                                   // the reply buffer: the head, then the columns
    {
        ReplyHead            head;
        fcdsp::HistoryColumn columns[kReplyMaxColumns];
    };

    inline constexpr std::size_t kReplyFixedBytes = offsetof(Reply, columns);
    inline constexpr std::size_t kMaxMessageBytes = sizeof(ParamsMsg);    // the largest record the engine accepts

    constexpr std::size_t replyBytes(std::uint32_t columnCount) noexcept
    {
        return kReplyFixedBytes + sizeof(fcdsp::HistoryColumn) * columnCount;
    }

    constexpr Header header(Kind kind, std::size_t bytes, std::uint32_t tag = 0) noexcept
    {
        return Header { kMagic, kVersion, static_cast<std::uint16_t>(kind), static_cast<std::uint32_t>(bytes), tag };
    }

    // ---- layouts (every record: standard layout, trivially copyable, fixed offsets, and a size that is the sum of its
    //      fields, so no padding) --------------------------------------------------------------------------------------
    namespace detail
    {
        template <class T>
        inline constexpr bool isRecord = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>
                                      && alignof(T) == 4 && sizeof(T) % 4 == 0;
    }

    static_assert(detail::isRecord<Header> && sizeof(Header) == 16);
    static_assert(offsetof(Header, magic) == 0 && offsetof(Header, version) == 4 && offsetof(Header, kind) == 6
                  && offsetof(Header, bytes) == 8 && offsetof(Header, tag) == 12);

    static_assert(kParamCount == 30, "the Params record carries the 30 parameters of Pid.h");
    static_assert(detail::isRecord<ParamsMsg> && sizeof(ParamsMsg) == 16 + 4 * 30 + 4);
    static_assert(offsetof(ParamsMsg, h) == 0 && offsetof(ParamsMsg, plain) == 16 && offsetof(ParamsMsg, snap) == 136);

    static_assert(detail::isRecord<AttachMsg> && sizeof(AttachMsg) == 20);
    static_assert(offsetof(AttachMsg, h) == 0 && offsetof(AttachMsg, attached) == 16);

    static_assert(detail::isRecord<ResetMsg> && sizeof(ResetMsg) == 16 && offsetof(ResetMsg, h) == 0);
    static_assert(detail::isRecord<PullMsg> && sizeof(PullMsg) == 16 && offsetof(PullMsg, h) == 0);

    static_assert(sizeof(fcdsp::UiFrame) == 288 && alignof(fcdsp::UiFrame) == 4);
    static_assert(sizeof(fcdsp::HistoryColumn) == 32 && alignof(fcdsp::HistoryColumn) == 4);
    static_assert(detail::isRecord<ReplyHead> && sizeof(ReplyHead) == 32 + 288);
    static_assert(offsetof(ReplyHead, h) == 0 && offsetof(ReplyHead, flags) == 16
                  && offsetof(ReplyHead, latencySamples) == 20 && offsetof(ReplyHead, columnCount) == 24
                  && offsetof(ReplyHead, firstColumn) == 28 && offsetof(ReplyHead, frame) == 32);
    static_assert(detail::isRecord<Reply> && sizeof(Reply) == 320 + 32 * kReplyMaxColumns);
    static_assert(offsetof(Reply, head) == 0 && offsetof(Reply, columns) == 320);
    static_assert(kReplyFixedBytes == 320 && replyBytes(kReplyMaxColumns) == sizeof(Reply));
    static_assert(kMaxMessageBytes == 140);
} // namespace fcmp::web
