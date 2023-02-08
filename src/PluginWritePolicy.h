#pragma once

#include <string.h>
#include <errno.h>
#include <spawn.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <memory>

#include "golpe.h"


enum class WritePolicyResult {
    Accept,
    Reject,
    ShadowReject,
};


struct PluginWritePolicy {
    struct RunningPlugin {
        pid_t pid;
        std::string currPluginPath;
        struct timespec lastModTime;
        FILE *r;
        FILE *w;

        RunningPlugin(pid_t pid, int rfd, int wfd, std::string currPluginPath) : pid(pid), currPluginPath(currPluginPath) {
            r = fdopen(rfd, "r");
            w = fdopen(wfd, "w");
            setlinebuf(w);
            {
                struct stat statbuf;
                if (stat(currPluginPath.c_str(), &statbuf)) throw herr("couldn't stat plugin: ", currPluginPath);
                lastModTime = statbuf.st_mtim;
            }
        }

        ~RunningPlugin() {
            fclose(r);
            fclose(w);
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
    };

    std::unique_ptr<RunningPlugin> running; 

    WritePolicyResult acceptEvent(std::string_view jsonStr, uint64_t receivedAt, EventSourceType sourceType, std::string_view sourceInfo, std::string &okMsg) {
        const auto &pluginPath = cfg().relay__writePolicy__plugin;

        if (pluginPath.size() == 0) {
            running.reset();
            return WritePolicyResult::Accept;
        }

        try {
            if (running) {
                if (pluginPath != running->currPluginPath) {
                    running.reset();
                } else {
                    struct stat statbuf;
                    if (stat(pluginPath.c_str(), &statbuf)) throw herr("couldn't stat plugin: ", pluginPath);
                    if (statbuf.st_mtim.tv_sec != running->lastModTime.tv_sec || statbuf.st_mtim.tv_nsec != running->lastModTime.tv_nsec) {
                        running.reset();
                    }
                }
            }

            if (!running) setupPlugin();

            auto json = tao::json::from_string(jsonStr);

            auto request = tao::json::value({
                { "type", "new" },
                { "event", json },
                { "receivedAt", receivedAt },
                { "sourceType", eventSourceTypeToStr(sourceType) },
                { "sourceInfo", sourceType == EventSourceType::IP4 || sourceType == EventSourceType::IP6 ? renderIP(sourceInfo) : sourceInfo },
            });

            std::string output = tao::json::to_string(request);
            output += "\n";

            ::fwrite(output.data(), output.size(), 1, running->w);

            tao::json::value response;

            while (1) {
                char buf[8192];
                if (!fgets(buf, sizeof(buf), running->r)) throw herr("pipe to plugin was closed (plugin crashed?)");

                try {
                    response = tao::json::from_string(buf);
                } catch (std::exception &e) {
                    LW << "Got unparseable line from write policy plugin: " << buf;
                    continue;
                }
                // FIXME: verify id

                break;
            }

            okMsg = response.optional<std::string>("msg").value_or("");

            auto action = response.at("action").get_string();
            if (action == "accept") return WritePolicyResult::Accept;
            else if (action == "reject") return WritePolicyResult::Reject;
            else if (action == "shadowReject") return WritePolicyResult::ShadowReject;
            else throw herr("unknown action: ", action);
        } catch (std::exception &e) {
            LE << "Couldn't setup PluginWritePolicy: " << e.what();
            running.reset();
            okMsg = "error: internal error";
            return WritePolicyResult::Reject;
        }
    }



    struct Pipe : NonCopyable {
        int fds[2] = { -1, -1 };

        Pipe() {
            if (::pipe(fds)) throw herr("pipe failed: ", strerror(errno));
        }

        Pipe(int fd0, int fd1) {
            fds[0] = fd0;
            fds[1] = fd1;
        }

        ~Pipe() {
            if (fds[0] != -1) ::close(fds[0]);
            if (fds[1] != -1) ::close(fds[1]);
        }

        int saveFd(int offset) {
            int fd = fds[offset];
            fds[offset] = -1;
            return fd;
        }
    };

    void setupPlugin() {
        auto path = cfg().relay__writePolicy__plugin;
        LI << "Setting up write policy plugin: " << path;

        Pipe outPipe;
        Pipe inPipe;

        pid_t pid;
        char *argv[] = { nullptr, };

        posix_spawn_file_actions_t file_actions;

        if (
            posix_spawn_file_actions_init(&file_actions) ||
            posix_spawn_file_actions_adddup2(&file_actions, outPipe.fds[0], 0) ||
            posix_spawn_file_actions_adddup2(&file_actions, inPipe.fds[1], 1) ||
            posix_spawn_file_actions_addclose(&file_actions, outPipe.fds[0]) ||
            posix_spawn_file_actions_addclose(&file_actions, outPipe.fds[1]) ||
            posix_spawn_file_actions_addclose(&file_actions, inPipe.fds[0]) ||
            posix_spawn_file_actions_addclose(&file_actions, inPipe.fds[1])
        ) throw herr("posix_span_file_actions failed: ", strerror(errno));

        auto ret = posix_spawn(&pid, path.c_str(), &file_actions, nullptr, argv, nullptr);
        if (ret) throw herr("posix_spawn failed when to invoke '", path, "': ", strerror(errno));

        running = make_unique<RunningPlugin>(pid, inPipe.saveFd(0), outPipe.saveFd(1), path);
    }
};
