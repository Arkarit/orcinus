/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * orcinus-sandbox: runs an AI agent so that it reaches only the folders it is given (AI5d).
 *
 *   orcinus-sandbox [--bind PATH]... [--ro-bind PATH]... [--chdir DIR] [--control FD] -- PROGRAM [ARGUMENT]...
 *   orcinus-sandbox --check
 *
 * The agent sees a root of its own: the system (/usr, /etc, /opt, /var, …) read-only, fresh
 * /tmp, /run, /dev/shm and /var/tmp, its own /proc, and the given paths at their real places
 * (--bind read-write, --ro-bind read-only; missing ones are left out; outer ones first, so a
 * folder inside a read-only one can be read-write). Everything else, the
 * rest of the home folder, /media, /mnt, /srv and /run/user (the session bus would let it run
 * commands outside), is not there.
 *
 * Two levels, both in a user namespace of our own (no root; on Ubuntu the AppArmor profile
 * orcinus-sandbox allows it):
 *  - this process: a mount namespace that still sees the host and holds the agent's root as a
 *    shared tmpfs, so folders can be added later (AI5d3);
 *  - the agent: a mount namespace whose root is a slave of that tmpfs (pivot_root), and a PID
 *    namespace. It runs as the user, without capabilities.
 * The root is built on an empty folder of the user's runtime directory ($XDG_RUNTIME_DIR/orcinus-sandbox,
 * else /tmp/.orcinus-sandbox-UID), covered by a tmpfs in our namespace only: the rest of the host
 * stays visible here, so folders anywhere can be added later.
 *
 * --control FD: a socket on which Orcinus adds and removes folders while the agent runs, one
 * line each, "add PATH" or "remove PATH" (absolute, read-write); each line is answered "ok" or
 * "error MESSAGE". The agent does not get the socket.
 *
 * Signals to this process go to the agent; its exit status is ours. Errors of the sandbox
 * itself end with status 125 and a line on stderr starting with "orcinus-sandbox:".
 *
 * See .privatedoc/ai-support-plan.md (AI5d).
 */

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
constexpr int SetupFailed = 125;
/** The agent's root, a shared tmpfs (see main()). */
std::string Root;
/** Folders shared now, by their path; added and removed through the control socket. */
std::set<std::string> g_shared;

struct Bind {
    std::string path;
    bool readOnly = false;
    int fd = -1; ///< opened before anything is mounted over its place
};

[[noreturn]] void fail(const std::string &what, int error = errno)
{
    std::string message = "orcinus-sandbox: " + what;
    if (error) {
        message += ": ";
        message += std::strerror(error);
    }
    std::fprintf(stderr, "%s\n", message.c_str());
    std::_Exit(SetupFailed);
}

void writeFile(const char *path, const std::string &content)
{
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, content.data(), content.size()) != ssize_t(content.size())) {
        if (errno == EPERM || errno == EACCES) {
            fail(std::string("user namespaces are not allowed (") + path + "); on Ubuntu the AppArmor profile for orcinus-sandbox must be loaded", 0);
        }
        fail(std::string("cannot write ") + path);
    }
    close(fd);
}

void mountOrFail(const char *source, const std::string &target, const char *type, unsigned long flags, const char *data = nullptr)
{
    if (mount(source, target.c_str(), type, flags, data) != 0) {
        fail("cannot mount " + target);
    }
}

void makeReadOnly(const std::string &target)
{
    mount_attr attr{};
    attr.attr_set = MOUNT_ATTR_RDONLY;
    if (mount_setattr(AT_FDCWD, target.c_str(), AT_RECURSIVE, &attr, sizeof(attr)) != 0) {
        fail("cannot make " + target + " read-only");
    }
}

/** mkdir -p below the root; @return false if a part cannot be made. */
bool makePath(const std::string &path, bool lastIsFile)
{
    std::string current;
    std::size_t start = 1;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        current = path.substr(0, end);
        const bool last = end == path.size();
        struct stat st;
        if (lstat(current.c_str(), &st) != 0) {
            if (last && lastIsFile) {
                const int fd = open(current.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
                if (fd < 0) {
                    return false;
                }
                close(fd);
            } else if (mkdir(current.c_str(), 0755) != 0) {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

std::string fdPath(int fd)
{
    return "/proc/self/fd/" + std::to_string(fd);
}

/** The system parts of the root: what an agent needs to run, nothing of the user's data. */
void buildRoot()
{
    static const char *const systemDirs[] = {"usr", "etc", "opt", "var", "bin", "sbin", "lib", "lib32", "lib64", "libx32", "nix", "sys"};
    for (const char *name : systemDirs) {
        const std::string source = std::string("/") + name;
        const std::string target = std::string(Root) + "/" + name;
        struct stat st;
        if (lstat(source.c_str(), &st) != 0) {
            continue;
        }
        if (S_ISLNK(st.st_mode)) {
            // Merged /usr: /bin -> usr/bin and so on.
            char link[4096];
            const ssize_t size = readlink(source.c_str(), link, sizeof(link) - 1);
            if (size > 0) {
                link[size] = '\0';
                if (symlink(link, target.c_str()) != 0) {
                    fail("cannot link " + target);
                }
            }
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            continue;
        }
        mkdir(target.c_str(), 0755);
        mountOrFail(source.c_str(), target, nullptr, MS_BIND | MS_REC);
        makeReadOnly(target);
    }

    const std::string &root = Root;
    mkdir((root + "/dev").c_str(), 0755);
    mountOrFail("/dev", root + "/dev", nullptr, MS_BIND | MS_REC);
    struct stat st;
    if (stat((root + "/dev/shm").c_str(), &st) == 0) {
        mountOrFail("tmpfs", root + "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    }
    for (const char *name : {"/tmp", "/run"}) {
        mkdir((root + name).c_str(), 0755);
        mountOrFail("tmpfs", root + name, "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    }
    if (stat((root + "/var/tmp").c_str(), &st) == 0) {
        mountOrFail("tmpfs", root + "/var/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    }
    // Name resolution: /etc/resolv.conf links into /run on many systems.
    for (const char *name : {"/run/systemd/resolve", "/run/NetworkManager", "/run/resolvconf"}) {
        if (stat(name, &st) == 0 && S_ISDIR(st.st_mode)) {
            const std::string target = root + name;
            if (makePath(target, false)) {
                mountOrFail(name, target, nullptr, MS_BIND | MS_REC);
                makeReadOnly(target);
            }
        }
    }
    mkdir((root + "/proc").c_str(), 0555);
}

void addBind(const Bind &bind)
{
    const std::string target = std::string(Root) + bind.path;
    struct stat st;
    if (fstat(bind.fd, &st) != 0) {
        fail("cannot read " + bind.path);
    }
    if (!makePath(target, !S_ISDIR(st.st_mode))) {
        fail("cannot make the place for " + bind.path);
    }
    mountOrFail(fdPath(bind.fd).c_str(), target, nullptr, MS_BIND | MS_REC);
    if (bind.readOnly) {
        makeReadOnly(target);
    } else {
        g_shared.insert(bind.path);
    }
}

/** @return An error message, empty on success. */
std::string share(const std::string &path)
{
    if (path.empty() || path[0] != '/' || path.find("/../") != std::string::npos || path.find("/./") != std::string::npos) {
        return "not a clean absolute path";
    }
    if (g_shared.count(path)) {
        return std::string();
    }
    const int fd = open(path.c_str(), O_PATH | O_CLOEXEC);
    if (fd < 0) {
        return std::strerror(errno);
    }
    struct stat st;
    std::string error;
    const std::string target = Root + path;
    if (fstat(fd, &st) != 0) {
        error = std::strerror(errno);
    } else if (!makePath(target, !S_ISDIR(st.st_mode))) {
        error = std::string("no place for it: ") + std::strerror(errno);
    } else if (mount(fdPath(fd).c_str(), target.c_str(), nullptr, MS_BIND | MS_REC, nullptr) != 0) {
        error = std::strerror(errno);
    } else {
        g_shared.insert(path);
    }
    close(fd);
    return error;
}

std::string unshare(const std::string &path)
{
    if (!g_shared.count(path)) {
        return "not shared";
    }
    const std::string target = Root + path;
    if (umount2(target.c_str(), MNT_DETACH) != 0) {
        return std::strerror(errno);
    }
    g_shared.erase(path);
    // The place it had, and parents that are empty now (they were made for it).
    std::string place = target;
    while (place.size() > Root.size() && (rmdir(place.c_str()) == 0 || unlink(place.c_str()) == 0)) {
        place.erase(place.rfind('/'));
    }
    return std::string();
}

void serveControl(int fd)
{
    std::string buffer;
    char chunk[4096];
    for (;;) {
        const ssize_t size = read(fd, chunk, sizeof(chunk));
        if (size <= 0) {
            if (size < 0 && errno == EINTR) {
                continue;
            }
            return;
        }
        buffer.append(chunk, size);
        std::size_t end;
        while ((end = buffer.find('\n')) != std::string::npos) {
            const std::string line = buffer.substr(0, end);
            buffer.erase(0, end + 1);
            std::string error;
            if (line.rfind("add ", 0) == 0) {
                error = share(line.substr(4));
            } else if (line.rfind("remove ", 0) == 0) {
                error = unshare(line.substr(7));
            } else {
                error = "unknown command";
            }
            const std::string answer = error.empty() ? "ok\n" : "error " + error + "\n";
            if (write(fd, answer.data(), answer.size()) < 0) {
                return;
            }
        }
    }
}

/** The empty folder of the host the root is built on; made if needed. */
std::string holderPath()
{
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    std::string path;
    if (runtime && runtime[0] == '/') {
        path = std::string(runtime) + "/orcinus-sandbox";
    } else {
        path = "/tmp/.orcinus-sandbox-" + std::to_string(getuid());
    }
    mkdir(path.c_str(), 0700);
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
        fail("cannot use " + path, 0);
    }
    return path;
}

pid_t g_child = 0;

void forward(int signal)
{
    if (g_child > 0) {
        kill(g_child, signal);
    }
}

void forwardSignals()
{
    struct sigaction action{};
    action.sa_handler = forward;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGTERM, SIGINT, SIGHUP, SIGQUIT, SIGUSR1, SIGUSR2}) {
        sigaction(signal, &action, nullptr);
    }
}

/**
 * Dies with the parent, also if it died before this was set. @p parent 0: the parent is outside
 * the PID namespace (getppid() says 0 then), only the signal is set.
 */
void dieWithParent(pid_t parent)
{
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (parent != 0 && getppid() != parent) {
        std::_Exit(SetupFailed);
    }
}

int exitStatus(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return SetupFailed;
}

/** Waits for @p child, reaping everything else (as PID 1 of the namespace). */
int waitFor(pid_t child)
{
    for (;;) {
        int status = 0;
        const pid_t pid = waitpid(-1, &status, 0);
        if (pid == child) {
            return exitStatus(status);
        }
        if (pid < 0 && errno != EINTR) {
            return SetupFailed;
        }
    }
}

[[noreturn]] void runAgent(const std::string &directory, char **argv)
{
    // PID 1 of the new PID namespace, in the agent's mount namespace.
    const std::string &root = Root;
    mountOrFail("proc", root + "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC);
    if (chdir(Root.c_str()) != 0 || syscall(SYS_pivot_root, ".", ".") != 0 || umount2(".", MNT_DETACH) != 0) {
        fail("cannot enter the sandbox");
    }
    if (chdir("/") != 0) {
        fail("cannot enter the sandbox");
    }
    if (!directory.empty() && chdir(directory.c_str()) != 0) {
        fail("cannot change to " + directory);
    }

    const pid_t self = getpid();
    const pid_t agent = fork();
    if (agent < 0) {
        fail("cannot start the agent");
    }
    if (agent == 0) {
        dieWithParent(self);
        signal(SIGPIPE, SIG_DFL);
        // A user, not root, in the namespace: execve drops all capabilities.
        execvp(argv[0], argv);
        fail(std::string("cannot run ") + argv[0]);
    }
    g_child = agent;
    forwardSignals();
    std::_Exit(waitFor(agent));
}
}

int main(int argc, char **argv)
{
    std::vector<Bind> binds;
    std::string directory;
    int control = -1;
    int first = argc;
    char trueCommand[] = "true";
    char *checkArgv[] = {trueCommand, nullptr};
    char **command = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--") {
            first = i + 1;
            break;
        }
        if (arg == "--check") {
            command = checkArgv;
            continue;
        }
        if (arg == "--control" && i + 1 < argc) {
            control = std::atoi(argv[++i]);
            if (control < 3 || fcntl(control, F_SETFD, FD_CLOEXEC) != 0) {
                fail("--control needs an open file descriptor", 0);
            }
            continue;
        }
        if ((arg == "--bind" || arg == "--ro-bind" || arg == "--chdir") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value.empty() || value[0] != '/') {
                fail(arg + " needs an absolute path: " + value, 0);
            }
            if (arg == "--chdir") {
                directory = value;
            } else {
                binds.push_back(Bind{value, arg == "--ro-bind"});
            }
            continue;
        }
        fail("unknown argument " + arg + "; usage: orcinus-sandbox [--bind PATH]... [--ro-bind PATH]... [--chdir DIR] [--control FD] -- PROGRAM [ARGUMENT]...",
             0);
    }
    if (!command) {
        if (first >= argc) {
            fail("no program given", 0);
        }
        command = argv + first;
    }

    const std::string holder = holderPath();
    const uid_t uid = getuid();
    const gid_t gid = getgid();
    if (::unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
        fail("cannot create a user namespace");
    }
    writeFile("/proc/self/setgroups", "deny");
    writeFile("/proc/self/uid_map", std::to_string(uid) + " " + std::to_string(uid) + " 1");
    writeFile("/proc/self/gid_map", std::to_string(gid) + " " + std::to_string(gid) + " 1");

    mountOrFail(nullptr, "/", nullptr, MS_REC | MS_PRIVATE);
    // In this namespace (a bind needs a mount of it). Outer paths first: a read-only folder must not cover what is shared read-write inside it.
    std::stable_sort(binds.begin(), binds.end(), [](const Bind &a, const Bind &b) {
        return std::count(a.path.begin(), a.path.end(), '/') < std::count(b.path.begin(), b.path.end(), '/');
    });
    std::vector<Bind> found;
    for (Bind &bind : binds) {
        bind.fd = open(bind.path.c_str(), O_PATH | O_CLOEXEC);
        if (bind.fd >= 0) {
            found.push_back(bind);
        }
    }
    // Over the empty holder of this namespace only; the host keeps it empty.
    mountOrFail("tmpfs", holder, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0700");
    Root = holder + "/root";
    mkdir(Root.c_str(), 0755);
    mountOrFail("tmpfs", Root, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
    mountOrFail(nullptr, Root, nullptr, MS_SHARED);
    buildRoot();
    for (const Bind &bind : found) {
        addBind(bind);
        close(bind.fd);
    }

    const pid_t self = getpid();
    const pid_t child = fork();
    if (child < 0) {
        fail("cannot start the sandbox");
    }
    if (child == 0) {
        dieWithParent(self);
        if (::unshare(CLONE_NEWNS | CLONE_NEWPID) != 0) {
            fail("cannot create the agent's namespaces");
        }
        // Mounts added to the root later reach the agent, none of its own go back.
        mountOrFail(nullptr, "/", nullptr, MS_REC | MS_SLAVE);
        const pid_t init = fork();
        if (init < 0) {
            fail("cannot start the sandbox");
        }
        if (init == 0) {
            dieWithParent(0);
            runAgent(directory, command);
        }
        g_child = init;
        forwardSignals();
        std::_Exit(waitFor(init));
    }
    g_child = child;
    forwardSignals();
    if (control >= 0) {
        std::thread(serveControl, control).detach();
    }
    return waitFor(child);
}
