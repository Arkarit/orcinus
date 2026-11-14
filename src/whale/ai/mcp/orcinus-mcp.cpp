/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * orcinus-mcp: the MCP server the AI panel passes to an agent (AI6). The agent starts it as a
 * stdio MCP server; it only relays newline-delimited JSON-RPC between stdio and the panel's
 * socket of the conversation, where WhaleAiMcpServer answers. All checks are there: whoever can
 * open the socket gets nothing this program could add or take away.
 *
 *   orcinus-mcp --socket PATH --token TOKEN
 *
 * The token is the first line on the socket. No Qt: it starts with every conversation, also
 * inside orcinus-sandbox. See .privatedoc/ai-support-plan.md (AI6).
 */

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
[[noreturn]] void fail(const std::string &message)
{
    std::fprintf(stderr, "orcinus-mcp: %s\n", message.c_str());
    std::_Exit(1);
}

/** @return false if the other side is gone. */
bool writeAll(int fd, const char *data, size_t size)
{
    while (size > 0) {
        const ssize_t written = ::write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data += written;
        size -= size_t(written);
    }
    return true;
}
}

int main(int argc, char **argv)
{
    std::string socketPath;
    std::string token;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--socket" || arg == "--token") && i + 1 < argc) {
            (arg == "--socket" ? socketPath : token) = argv[++i];
            continue;
        }
        fail("unknown argument " + arg + "; usage: orcinus-mcp --socket PATH --token TOKEN");
    }
    if (socketPath.empty() || token.empty()) {
        fail("usage: orcinus-mcp --socket PATH --token TOKEN");
    }
    std::signal(SIGPIPE, SIG_IGN);

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(address.sun_path)) {
        fail("socket path too long");
    }
    std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);
    const int socket = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket < 0 || ::connect(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        fail("cannot reach Orcinus at " + socketPath + ": " + std::strerror(errno));
    }
    const std::string hello = token + "\n";
    if (!writeAll(socket, hello.data(), hello.size())) {
        fail("Orcinus closed the connection");
    }

    pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {socket, POLLIN, 0}};
    char buffer[65536];
    while (true) {
        if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail(std::string("poll: ") + std::strerror(errno));
        }
        if (fds[0].revents) {
            const ssize_t size = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (size > 0) {
                if (!writeAll(socket, buffer, size_t(size))) {
                    return 0;
                }
            } else if (size == 0 || errno != EINTR) {
                // The agent is done; Orcinus closes its side, then we end.
                ::shutdown(socket, SHUT_WR);
                fds[0].fd = -1;
            }
        }
        if (fds[1].revents) {
            const ssize_t size = ::read(socket, buffer, sizeof(buffer));
            if (size > 0) {
                if (!writeAll(STDOUT_FILENO, buffer, size_t(size))) {
                    return 0;
                }
            } else if (size == 0 || errno != EINTR) {
                return 0;
            }
        }
    }
}
