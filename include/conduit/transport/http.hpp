/**
 * @file http.hpp
 * @author Kristóf Barta
 * © 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include "../core.hpp"
#include <string_view>
#include <array>

namespace cre::transport {

    // ============================================================
    // 1. THE ZERO‑COPY HTTP EVENT (POD Structure)
    // ============================================================
    struct http_header {
        std::string_view key;
        std::string_view value;
    };

    struct http_request_event : conduit::core::allocated_event<http_request_event, 200> {
        uint32_t connection_id;

        // Pointers into raw memory (string_view is just a pointer + length)
        std::string_view method;
        std::string_view uri;

        // Fixed‑size array for headers (O(1) allocation)
        static constexpr size_t MAX_HEADERS = 16;
        std::array<http_header, MAX_HEADERS> headers;
        size_t header_count{ 0 };

        // The Payload (e.g., a JSON)
        std::string_view body;
    };

    // ============================================================
    // 2. THE DETERMINISTIC STATE MACHINE (DFA Parser)
    // ============================================================
    class http_parser {
        enum class state { METHOD, URI, VERSION, HEADER_KEY, HEADER_VALUE, BODY, DONE };

    public:
        // O(1) parse function (Zero allocation, zero copying)
        // Input: raw byte buffer read from the network
        static bool parse(const char* raw_buffer, size_t length, http_request_event& out_event) noexcept {
            state current_state = state::METHOD;
            size_t token_start = 0;

            for (size_t i = 0; i < length; ++i) {
                char c = raw_buffer[i];

                switch (current_state) {
                case state::METHOD:
                    if (c == ' ') {
                        out_event.method = std::string_view(raw_buffer + token_start, i - token_start);
                        token_start = i + 1;
                        current_state = state::URI;
                    }
                    break;

                case state::URI:
                    if (c == ' ') {
