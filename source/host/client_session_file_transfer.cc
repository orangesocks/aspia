//
// Aspia Project
// Copyright (C) 2016-2023 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "host/client_session_file_transfer.h"

#include "build/build_config.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/task_runner.h"
#include "base/files/base_paths.h"
#include "proto/file_transfer.pb.h"

#if defined(OS_WIN)
#include "base/win/scoped_object.h"

#include <UserEnv.h>
#include <WtsApi32.h>
#endif // defined(OS_WIN)

namespace host {

namespace {

#if defined(OS_WIN)
const char16_t kFileTransferAgentFile[] = u"aspia_file_transfer_agent.exe";
const char16_t kDefaultDesktopName[] = u"winsta0\\default";

//--------------------------------------------------------------------------------------------------
bool createLoggedOnUserToken(DWORD session_id, base::win::ScopedHandle* token_out)
{
    base::win::ScopedHandle user_token;
    if (!WTSQueryUserToken(session_id, user_token.recieve()))
    {
        PLOG(LS_WARNING) << "WTSQueryUserToken failed";
        return false;
    }

    TOKEN_ELEVATION_TYPE elevation_type;
    DWORD returned_length;

    if (!GetTokenInformation(user_token,
                             TokenElevationType,
                             &elevation_type,
                             sizeof(elevation_type),
                             &returned_length))
    {
        PLOG(LS_WARNING) << "GetTokenInformation failed";
        return false;
    }

    switch (elevation_type)
    {
        // The token is a limited token.
        case TokenElevationTypeLimited:
        {
            TOKEN_LINKED_TOKEN linked_token_info;

            // Get the unfiltered token for a silent UAC bypass.
            if (!GetTokenInformation(user_token,
                                     TokenLinkedToken,
                                     &linked_token_info,
                                     sizeof(linked_token_info),
                                     &returned_length))
            {
                PLOG(LS_WARNING) << "GetTokenInformation failed";
                return false;
            }

            // Attach linked token.
            token_out->reset(linked_token_info.LinkedToken);
        }
        break;

        case TokenElevationTypeDefault: // The token does not have a linked token.
        case TokenElevationTypeFull:    // The token is an elevated token.
        default:
            token_out->reset(user_token.release());
            break;
    }

    return true;
}

//--------------------------------------------------------------------------------------------------
bool startProcessWithToken(HANDLE token,
                           const base::CommandLine& command_line,
                           base::win::ScopedHandle* process,
                           base::win::ScopedHandle* thread)
{
    STARTUPINFOW startup_info;
    memset(&startup_info, 0, sizeof(startup_info));

    startup_info.cb = sizeof(startup_info);
    startup_info.lpDesktop = const_cast<wchar_t*>(
        reinterpret_cast<const wchar_t*>(kDefaultDesktopName));

    void* environment = nullptr;

    if (!CreateEnvironmentBlock(&environment, token, FALSE))
    {
            PLOG(LS_WARNING) << "CreateEnvironmentBlock failed";
            return false;
    }

    PROCESS_INFORMATION process_info;
    memset(&process_info, 0, sizeof(process_info));

    if (!CreateProcessAsUserW(token,
                              nullptr,
                              const_cast<wchar_t*>(reinterpret_cast<const wchar_t*>(
                                  command_line.commandLineString().c_str())),
                              nullptr,
                              nullptr,
                              FALSE,
                              CREATE_UNICODE_ENVIRONMENT | HIGH_PRIORITY_CLASS,
                              environment,
                              nullptr,
                              &startup_info,
                              &process_info))
    {
            PLOG(LS_WARNING) << "CreateProcessAsUserW failed";
            if (!DestroyEnvironmentBlock(environment))
            {
                PLOG(LS_WARNING) << "DestroyEnvironmentBlock failed";
            }
            return false;
    }

    thread->reset(process_info.hThread);
    process->reset(process_info.hProcess);

    if (!DestroyEnvironmentBlock(environment))
    {
            PLOG(LS_WARNING) << "DestroyEnvironmentBlock failed";
    }

    return true;
}
#endif // defined(OS_WIN)

std::filesystem::path agentFilePath()
{
    std::filesystem::path file_path;
    if (!base::BasePaths::currentExecDir(&file_path))
    {
            LOG(LS_WARNING) << "currentExecDir failed";
            return std::filesystem::path();
    }

    file_path.append(kFileTransferAgentFile);
    return file_path;
}

} // namespace

//--------------------------------------------------------------------------------------------------
ClientSessionFileTransfer::ClientSessionFileTransfer(std::unique_ptr<base::TcpChannel> channel,
                                                     std::shared_ptr<base::TaskRunner> task_runner)
    : ClientSession(proto::SESSION_TYPE_FILE_TRANSFER, std::move(channel)),
      task_runner_(task_runner),
      attach_timer_(std::make_unique<base::WaitableTimer>(
          base::WaitableTimer::Type::SINGLE_SHOT, task_runner))
{
    LOG(LS_INFO) << "Ctor";
}

//--------------------------------------------------------------------------------------------------
ClientSessionFileTransfer::~ClientSessionFileTransfer()
{
    LOG(LS_INFO) << "Dtor";
}

//--------------------------------------------------------------------------------------------------
void ClientSessionFileTransfer::onStarted()
{
    std::u16string channel_id = base::IpcServer::createUniqueId();

    ipc_server_ = std::make_unique<base::IpcServer>();
    if (!ipc_server_->start(channel_id, this))
    {
        LOG(LS_ERROR) << "Failed to start IPC server (channel_id: " << channel_id << ")";
        onError(FROM_HERE);
        return;
    }

#if defined(OS_WIN)
    base::CommandLine command_line(agentFilePath());
    command_line.appendSwitch(u"channel_id", channel_id);

    base::win::ScopedHandle session_token;
    if (!createLoggedOnUserToken(sessionId(), &session_token))
    {
        LOG(LS_WARNING) << "createSessionToken failed";
        return;
    }

    base::win::ScopedHandle process_handle;
    base::win::ScopedHandle thread_handle;
    if (!startProcessWithToken(session_token, command_line, &process_handle, &thread_handle))
    {
        LOG(LS_WARNING) << "startProcessWithToken failed";
        onError(FROM_HERE);
        return;
    }
#else
    NOTIMPLEMENTED();
    onErrorOccurred();
#endif

    LOG(LS_INFO) << "Wait for starting agent process";
    has_logged_on_user_ = true;

    attach_timer_->start(std::chrono::seconds(10), [this]()
    {
        LOG(LS_WARNING) << "Timeout at the start of the session process";
        onError(FROM_HERE);
    });
}

//--------------------------------------------------------------------------------------------------
void ClientSessionFileTransfer::onReceived(uint8_t /* channel_id */, const base::ByteArray& buffer)
{
    if (!has_logged_on_user_)
    {
        proto::FileReply reply;
        reply.set_error_code(proto::FILE_ERROR_NO_LOGGED_ON_USER);

        sendMessage(proto::HOST_CHANNEL_ID_SESSION, base::serialize(reply));
        return;
    }

    if (ipc_channel_)
    {
        ipc_channel_->send(base::ByteArray(buffer));
    }
    else
    {
        // IPC channel not connected yet.
        pending_messages_.emplace_back(buffer);
    }
}

//--------------------------------------------------------------------------------------------------
void ClientSessionFileTransfer::onWritten(uint8_t /* channel_id */, size_t /* pending */)
{
    // Nothing
}

//--------------------------------------------------------------------------------------------------
void ClientSessionFileTransfer::onNewConnection(std::unique_ptr<base::IpcChannel> channel)
{
    LOG(LS_INFO) << "IPC channel for file transfer session is connected";

    task_runner_->deleteSoon(std::move(ipc_server_));
    attach_timer_->stop();

    ipc_channel_ = std::move(channel);
    ipc_channel_->setListener(this);
    ipc_channel_->resume();

    for (auto& message : pending_messages_)
        ipc_channel_->send(std::move(message));

    pending_messages_.clear();
}

//--------------------------------------------------------------------------------------------------
void ClientSessionFileTransfer::onErrorOccurred()
{
    onError(FROM_HERE);
}

//--------------------------------------------------------------------------------------------------
void ClientSessionFileTransfer::onIpcDisconnected()
{
    onError(FROM_HERE);
}

//--------------------------------------------------------------------------------------------------
void ClientSessionFileTransfer::onIpcMessageReceived(const base::ByteArray& buffer)
{
    sendMessage(proto::HOST_CHANNEL_ID_SESSION, base::ByteArray(buffer));
}

//--------------------------------------------------------------------------------------------------
void ClientSessionFileTransfer::onError(const base::Location& location)
{
    LOG(LS_WARNING) << "Error occurred (from: " << location.toString() << ")";
    stop();
}

} // namespace host
