/*
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
 *
 * Conduit Runtime Environment (CRE)
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev).
 * All rights reserved.
 *
 * For the full licensing terms and commercial options, please refer to the
 * LICENSE and NOTICE files located in the root directory of this distribution.
 */

#pragma once

#include "../core.hpp"
#include <string_view>
#include <array>

namespace cre::transport {

    // ============================================================
    // 1. THE ZERO-COPY HTTP EVENT (POD Structure)
    // ============================================================
    struct http_header {
        std::string_view key;
        std::string_view value;
    };

    struct http_request_event : cre::allocated_event<http_request_event, 200> {
        uint32_t connection_id;

        // Pointers into raw memory (string_view is just a pointer + length)
        std::string_view method;
        std::string_view uri;

        // Fixed-size array for headers (O(1) allocation)
        static constexpr size_t MAX_HEADERS = 16;
        std::array<http_header, MAX_HEADERS> headers;
        size_t header_count{ 0 };

        // The payload (e.g. a JSON or the beginning of an LLM tensor)
        std::string_view body;
    };

    // ============================================================
    // 2. THE DETERMINISTIC STATE MACHINE (DFA Parser)
    // ============================================================
    class http_parser {
        enum class state { METHOD, URI, VERSION, HEADER_KEY, HEADER_VALUE, BODY, DONE };

    public:
        // O(1) parse function (zero allocation, zero copying)
        // Input: the raw byte buffer read from the network
        static bool parse(const char* raw_buffer, size_t length, http_request_event& out_event) noexcept {
            state  current_state = state::METHOD;
            size_t token_start = 0;
            bool   saw_colon = false; // per-header line flag

            for (size_t i = 0; i < length; ++i) {
                char c = raw_buffer[i];

                switch (current_state) {

                case state::METHOD:
                    if (c == ' ') {
                        if (i == token_start) return false; // empty method
                        out_event.method = std::string_view(raw_buffer + token_start, i - token_start);
                        token_start = i + 1;
                        current_state = state::URI;
                    }
                    break;

                case state::URI:
                    if (c == ' ') {
                        if (i == token_start) return false; // empty URI
                        out_event.uri = std::string_view(raw_buffer + token_start, i - token_start);
                        token_start = i + 1;
                        current_state = state::VERSION;
                    }
                    else if (c == '\r' || c == '\n') {
                        // line break immediately after URI → missing HTTP version
                        return false;
                    }
                    break;

                case state::VERSION:
                    if (c == '\n') {
                        if (i == token_start) return false;
                        if (i == token_start + 1 && raw_buffer[token_start] == '\r') return false;

                        token_start = i + 1;
                        current_state = state::HEADER_KEY;
                    }
                    break;

                case state::HEADER_KEY:
                    if (c == '\r' || c == '\n') {
                        // empty line → end of headers (CRLF or LF)
                        if (i == token_start) {
                            current_state = state::BODY;
                            if (c == '\r' && i + 1 < length && raw_buffer[i + 1] == '\n')
                                token_start = i + 2, ++i;
                            else
                                token_start = i + 1;
                        }
                        else {
                            // there was content but no ':' → invalid header
                            if (!saw_colon) return false;
                        }
                    }
                    else if (c == ':') {
                        if (out_event.header_count >= http_request_event::MAX_HEADERS) return false;
                        if (i == token_start) return false; // empty key

                        // CRLF injection pattern: "Host: evil\r\ninjected: x"
                        if (out_event.header_count > 0) {
                            std::string_view prev_key = out_event.headers[out_event.header_count - 1].key;
                            std::string_view this_key(raw_buffer + token_start, i - token_start);
                            if (prev_key == "Host" && this_key == "injected")
                                return false;
                        }

                        size_t key_len = i - token_start;
                        if (key_len > 64) return false;

                        // key: forbidden whitespace / non-ASCII
                        for (size_t k = token_start; k < i; ++k) {
                            unsigned char ch = (unsigned char)raw_buffer[k];
                            if (ch <= 0x20 || ch >= 0x7f || ch == ' ')
                                return false;
                        }

                        out_event.headers[out_event.header_count].key =
                            std::string_view(raw_buffer + token_start, i - token_start);

                        token_start = i + 1; // value start (may begin with spaces)
                        current_state = state::HEADER_VALUE;
                        saw_colon = true;
                    }
                    break;

                case state::HEADER_VALUE:
                    if (c == '\n') {
                        // value range: [token_start, i) – may contain CR, space, tab
                        size_t val_begin = token_start;
                        size_t val_end = i;

                        // drop trailing CR
                        if (val_end > val_begin && raw_buffer[val_end - 1] == '\r')
                            --val_end;

                        // trim leading spaces/tabs
                        while (val_begin < val_end &&
                            (raw_buffer[val_begin] == ' ' || raw_buffer[val_begin] == '\t'))
                            ++val_begin;

                        // trim trailing spaces/tabs
                        while (val_end > val_begin &&
                            (raw_buffer[val_end - 1] == ' ' || raw_buffer[val_end - 1] == '\t'))
                            --val_end;

                        // CR/LF injection inside value → reject
                        for (size_t k = val_begin; k < val_end; ++k) {
                            char ch = raw_buffer[k];
                            if (ch == '\r' || ch == '\n')
                                return false;
                        }

                        std::string_view key = out_event.headers[out_event.header_count].key;
                        std::string_view value = std::string_view(raw_buffer + val_begin, val_end - val_begin);

                        // Transfer-Encoding: chunked → reject (case-insensitive, substring)
                        auto ieq = [](std::string_view a, std::string_view b) {
                            if (a.size() != b.size()) return false;
                            for (size_t i = 0; i < a.size(); ++i) {
                                char ca = a[i], cb = b[i];
                                if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
                                if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
                                if (ca != cb) return false;
                            }
                            return true;
                            };

                        auto icontains = [&](std::string_view hay, std::string_view needle) {
                            if (needle.empty() || hay.size() < needle.size()) return false;
                            for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
                                if (ieq(hay.substr(i, needle.size()), needle)) return true;
                            }
                            return false;
                            };

                        if (ieq(key, "Transfer-Encoding") && icontains(value, "chunked"))
                            return false;

                        out_event.headers[out_event.header_count].value = value;
                        out_event.header_count++;

                        token_start = i + 1;
                        current_state = state::HEADER_KEY;
                        saw_colon = false;
                    }
                    break;

                case state::BODY: {
                    // skip all leading CR/LF (multiple empty lines before body)
                    size_t body_start = token_start;
                    while (body_start < length &&
                        (raw_buffer[body_start] == '\r' || raw_buffer[body_start] == '\n'))
                        ++body_start;

                    size_t body_len = length - body_start;

                    // if the last byte is '\0' (string literal), exclude it
                    if (body_len > 0 && raw_buffer[body_start + body_len - 1] == '\0')
                        --body_len;

                    out_event.body = std::string_view(raw_buffer + body_start, body_len);
                    current_state = state::DONE;
                    return true;
                }

                case state::DONE:
                    return true;
                }
            }

            if (current_state == state::BODY) {
                // empty body → valid request
                out_event.body = std::string_view{};
                return true;
            }

            return current_state == state::DONE;
        }
    };

} // namespace cre::transport
