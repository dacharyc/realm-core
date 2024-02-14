////////////////////////////////////////////////////////////////////////////
//
// Copyright 2017 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include <util/event_loop.hpp>
#include <util/test_file.hpp>
#include <util/test_utils.hpp>
#include <util/sync/session_util.hpp>

#include <realm/util/scope_exit.hpp>

#include <catch2/catch_all.hpp>

using namespace realm;
using namespace realm::util;

TEST_CASE("SyncSession: wait_for_download_completion() API", "[sync][pbs][session][completion]") {
    if (!EventLoop::has_implementation())
        return;

    TestSyncManager tsm({}, {false});
    auto& server = tsm.sync_server();
    auto sync_manager = tsm.sync_manager();
    std::atomic<bool> handler_called(false);

    SECTION("works properly when called after the session is bound") {
        server.start();
        auto user = tsm.fake_user();
        auto session = sync_session(user, "/async-wait-download-1", [](auto, auto) {});
        EventLoop::main().run_until([&] {
            return sessions_are_active(*session);
        });
        // Register the download-completion notification
        session->wait_for_download_completion([&](auto) {
            handler_called = true;
        });
        EventLoop::main().run_until([&] {
            return handler_called == true;
        });
    }

    SECTION("works properly when called on a logged-out session") {
        server.start();
        auto user = tsm.fake_user();
        auto session = sync_session(user, "/user-async-wait-download-3", [](auto, auto) {});
        EventLoop::main().run_until([&] {
            return sessions_are_active(*session);
        });
        // Log the user out, and wait for the sessions to log out.
        user->log_out();
        EventLoop::main().run_until([&] {
            return sessions_are_inactive(*session);
        });
        // Register the download-completion notification
        session->wait_for_download_completion([&](auto) {
            handler_called = true;
        });
        spin_runloop();
        REQUIRE(handler_called == false);
        // Log the user back in
        user = sync_manager->get_user(user->identity(), ENCODE_FAKE_JWT("not_a_real_token"),
                                      ENCODE_FAKE_JWT("not_a_real_token"), "");
        EventLoop::main().run_until([&] {
            return sessions_are_active(*session);
        });
        // Now, wait for the completion handler to be called.
        EventLoop::main().run_until([&] {
            return handler_called == true;
        });
    }

    SECTION("aborts properly when queued and the session errors out") {
        auto user = tsm.fake_user();
        std::atomic<int> error_count(0);
        std::shared_ptr<SyncSession> session = sync_session(user, "/async-wait-download-4", [&](auto, auto) {
            ++error_count;
        });
        Status err_status(ErrorCodes::SyncProtocolInvariantFailed, "Not a real error message");
        // Register the download-completion notification
        session->wait_for_download_completion([&](Status status) {
            REQUIRE(status == err_status);
            handler_called = true;
        });
        REQUIRE(handler_called == false);
        // Now trigger an error
        sync::SessionErrorInfo err{err_status, sync::IsFatal{true}};
        err.server_requests_action = sync::ProtocolErrorInfo::Action::ProtocolViolation;
        SyncSession::OnlyForTesting::handle_error(*session, std::move(err));
        EventLoop::main().run_until([&] {
            return error_count > 0;
        });
        REQUIRE(handler_called == true);
    }
}

TEST_CASE("SyncSession: wait_for_upload_completion() API", "[sync][pbs][session][completion]") {
    if (!EventLoop::has_implementation())
        return;

    TestSyncManager::Config config;
    config.should_teardown_test_directory = false;
    SyncServer::Config server_config = {false};
    TestSyncManager tsm(config, server_config);
    auto& server = tsm.sync_server();
    auto sync_manager = tsm.sync_manager();
    std::atomic<bool> handler_called(false);

    SECTION("works properly when called after the session is bound") {
        server.start();
        auto user = tsm.fake_user();
        auto session = sync_session(user, "/async-wait-upload-1", [](auto, auto) {});
        EventLoop::main().run_until([&] {
            return sessions_are_active(*session);
        });
        // Register the upload-completion notification
        session->wait_for_upload_completion([&](auto) {
            handler_called = true;
        });
        EventLoop::main().run_until([&] {
            return handler_called == true;
        });
    }

    SECTION("works properly when called on a logged-out session") {
        server.start();
        const auto user_id = "user-async-wait-upload-3";
        auto user = tsm.fake_user();
        auto session = sync_session(user, "/user-async-wait-upload-3", [](auto, auto) {});
        EventLoop::main().run_until([&] {
            return sessions_are_active(*session);
        });
        // Log the user out, and wait for the sessions to log out.
        user->log_out();
        EventLoop::main().run_until([&] {
            return sessions_are_inactive(*session);
        });
        // Register the upload-completion notification
        session->wait_for_upload_completion([&](auto) {
            handler_called = true;
        });
        spin_runloop();
        REQUIRE(handler_called == false);
        // Log the user back in
        user = sync_manager->get_user(user->identity(), ENCODE_FAKE_JWT("not_a_real_token"),
                                      ENCODE_FAKE_JWT("not_a_real_token"), "");
        EventLoop::main().run_until([&] {
            return sessions_are_active(*session);
        });
        // Now, wait for the completion handler to be called.
        EventLoop::main().run_until([&] {
            return handler_called == true;
        });
    }

    // FIXME: There seems to be a race condition here where the upload completion handler
    // FIXME: isn't actually called with the appropriate error, only the error handler is
    //    SECTION("aborts properly when queued and the session errors out") {
    //        using ProtocolError = realm::sync::ProtocolError;
    //        auto user = SyncManager::shared().get_user("user-async-wait-upload-4",
    //        ENCODE_FAKE_JWT("not_a_real_token"), ENCODE_FAKE_JWT("not_a_real_token"),
    //        dummy_device_id); std::atomic<int> error_count(0); std::shared_ptr<SyncSession> session =
    //        sync_session(user, "/async-wait-upload-4",
    //                                                            [&](auto e) {
    //            ++error_count;
    //        });
    //        std::error_code code = std::error_code{static_cast<int>(ProtocolError::bad_syntax),
    //        realm::sync::protocol_error_category()};
    //        // Register the upload-completion notification
    //        session->wait_for_upload_completion([&](std::error_code error) {
    //            CHECK(error == code);
    //            handler_called = true;
    //        });
    //        REQUIRE(handler_called == false);
    //        // Now trigger an error
    //        SyncSession::OnlyForTesting::handle_error(*session, {code, "Not a real error message", false});
    //        EventLoop::main().run_until([&] {
    //            return error_count > 0 && handler_called;
    //        });
    //        REQUIRE(handler_called == true);
    //    }
}
