#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include <boost/algorithm/string.hpp>

#include <gtest/gtest.h>

#include "olm/account.hh"

#include "client.hpp"
#include "crypto.hpp"

#include "mtx/requests.hpp"
#include "mtx/responses.hpp"
#include "variant.hpp"

using namespace mtx::client;
using namespace mtx::identifiers;
using namespace mtx::events::collections;

using namespace std;

using ErrType = std::experimental::optional<errors::ClientError>;

void
check_error(ErrType err)
{
        if (err) {
                cout << "matrix (error)  : " << err->matrix_error.error << "\n";
                cout << "matrix (errcode): " << mtx::errors::to_string(err->matrix_error.errcode)
                     << "\n";
                cout << "error_code      : " << err->error_code << "\n";
                cout << "status_code     : " << err->status_code << "\n";

                if (!err->parse_error.empty())
                        cout << "parse_error     : " << err->parse_error << "\n";
        }

        ASSERT_FALSE(err);
}

TEST(Encryption, UploadIdentityKeys)
{
        auto alice       = std::make_shared<Client>("localhost");
        auto olm_account = mtx::client::crypto::olm_new_account();

        alice->login(
          "alice", "secret", [](const mtx::responses::Login &, ErrType err) { check_error(err); });

        while (alice->access_token().empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto identity_keys = mtx::client::crypto::identity_keys(olm_account);

        ASSERT_TRUE(identity_keys.curve25519.size() > 10);
        ASSERT_TRUE(identity_keys.curve25519.size() > 10);

        mtx::client::crypto::OneTimeKeys unused;
        auto request = mtx::client::crypto::create_upload_keys_request(
          olm_account, identity_keys, unused, alice->user_id(), alice->device_id());

        // Make the request with the signed identity keys.
        alice->upload_keys(request, [](const mtx::responses::UploadKeys &res, ErrType err) {
                check_error(err);
                EXPECT_EQ(res.one_time_key_counts.size(), 0);
        });

        alice->close();
}

TEST(Encryption, UploadOneTimeKeys)
{
        auto alice       = std::make_shared<Client>("localhost");
        auto olm_account = mtx::client::crypto::olm_new_account();

        alice->login(
          "alice", "secret", [](const mtx::responses::Login &, ErrType err) { check_error(err); });

        while (alice->access_token().empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto nkeys = mtx::client::crypto::generate_one_time_keys(olm_account, 5);
        EXPECT_EQ(nkeys, 5);

        auto one_time_keys = mtx::client::crypto::one_time_keys(olm_account);

        mtx::requests::UploadKeys req;

        // Create the proper structure for uploading.
        std::map<std::string, json> unsigned_keys;

        auto obj = one_time_keys.at("curve25519");
        for (auto it = obj.begin(); it != obj.end(); ++it)
                unsigned_keys["curve25519:" + it.key()] = it.value();

        req.one_time_keys = unsigned_keys;

        alice->upload_keys(req, [](const mtx::responses::UploadKeys &res, ErrType err) {
                check_error(err);
                EXPECT_EQ(res.one_time_key_counts.size(), 1);
                EXPECT_EQ(res.one_time_key_counts.at("curve25519"), 5);
        });

        alice->close();
}

TEST(Encryption, UploadSignedOneTimeKeys)
{
        auto alice       = std::make_shared<Client>("localhost");
        auto olm_account = mtx::client::crypto::olm_new_account();

        alice->login(
          "alice", "secret", [](const mtx::responses::Login &, ErrType err) { check_error(err); });

        while (alice->access_token().empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto nkeys = mtx::client::crypto::generate_one_time_keys(olm_account, 5);
        EXPECT_EQ(nkeys, 5);

        auto one_time_keys = mtx::client::crypto::one_time_keys(olm_account);

        mtx::requests::UploadKeys req;
        req.one_time_keys = mtx::client::crypto::sign_one_time_keys(
          olm_account, one_time_keys, alice->user_id(), alice->device_id());

        alice->upload_keys(req, [nkeys](const mtx::responses::UploadKeys &res, ErrType err) {
                check_error(err);
                EXPECT_EQ(res.one_time_key_counts.size(), 1);
                EXPECT_EQ(res.one_time_key_counts.at("signed_curve25519"), nkeys);
        });

        alice->close();
}

TEST(Encryption, UploadKeys)
{
        auto alice       = std::make_shared<Client>("localhost");
        auto olm_account = mtx::client::crypto::olm_new_account();

        alice->login(
          "alice", "secret", [](const mtx::responses::Login &, ErrType err) { check_error(err); });

        while (alice->access_token().empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

        json identity_keys = mtx::client::crypto::identity_keys(olm_account);

        mtx::client::crypto::generate_one_time_keys(olm_account, 1);
        auto one_time_keys = mtx::client::crypto::one_time_keys(olm_account);

        auto req = mtx::client::crypto::create_upload_keys_request(
          olm_account, identity_keys, one_time_keys, alice->user_id(), alice->device_id());

        alice->upload_keys(req, [](const mtx::responses::UploadKeys &res, ErrType err) {
                check_error(err);
                EXPECT_EQ(res.one_time_key_counts.size(), 1);
                EXPECT_EQ(res.one_time_key_counts.at("signed_curve25519"), 1);
        });

        alice->close();
}

TEST(Encryption, QueryKeys)
{
        auto alice     = std::make_shared<Client>("localhost");
        auto alice_olm = mtx::client::crypto::olm_new_account();

        auto bob     = std::make_shared<Client>("localhost");
        auto bob_olm = mtx::client::crypto::olm_new_account();

        alice->login(
          "alice", "secret", [](const mtx::responses::Login &, ErrType err) { check_error(err); });

        bob->login(
          "bob", "secret", [](const mtx::responses::Login &, ErrType err) { check_error(err); });

        while (alice->access_token().empty() || bob->access_token().empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Create and upload keys for both users.
        auto alice_idks = mtx::client::crypto::identity_keys(alice_olm);
        auto bob_idks   = mtx::client::crypto::identity_keys(bob_olm);

        mtx::client::crypto::generate_one_time_keys(alice_olm, 1);
        mtx::client::crypto::generate_one_time_keys(bob_olm, 1);

        auto alice_otks = mtx::client::crypto::one_time_keys(alice_olm);
        auto bob_otks   = mtx::client::crypto::one_time_keys(bob_olm);

        auto alice_req = mtx::client::crypto::create_upload_keys_request(
          alice_olm, alice_idks, alice_otks, alice->user_id(), alice->device_id());

        auto bob_req = mtx::client::crypto::create_upload_keys_request(
          bob_olm, bob_idks, bob_otks, bob->user_id(), bob->device_id());

        // Validates that both upload requests are finished.
        atomic_int uploads(0);

        alice->upload_keys(alice_req,
                           [&uploads](const mtx::responses::UploadKeys &res, ErrType err) {
                                   check_error(err);
                                   EXPECT_EQ(res.one_time_key_counts.size(), 1);
                                   EXPECT_EQ(res.one_time_key_counts.at("signed_curve25519"), 1);

                                   uploads += 1;
                           });

        bob->upload_keys(bob_req, [&uploads](const mtx::responses::UploadKeys &res, ErrType err) {
                check_error(err);
                EXPECT_EQ(res.one_time_key_counts.size(), 1);
                EXPECT_EQ(res.one_time_key_counts.at("signed_curve25519"), 1);

                uploads += 1;
        });

        while (uploads != 2)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

        atomic_int responses(0);

        // Each user is requests each other's keys.
        mtx::requests::QueryKeys alice_rk;
        alice_rk.device_keys[bob->user_id().to_string()] = {};
        alice->query_keys(
          alice_rk, [&responses, bob, bob_req](const mtx::responses::QueryKeys &res, ErrType err) {
                  check_error(err);

                  ASSERT_TRUE(res.failures.size() == 0);

                  auto bob_devices = res.device_keys.at(bob->user_id().to_string());
                  ASSERT_TRUE(bob_devices.size() > 0);

                  auto dev_keys = bob_devices.at(bob->device_id());
                  EXPECT_EQ(dev_keys.user_id, bob->user_id().to_string());
                  EXPECT_EQ(dev_keys.device_id, bob->device_id());
                  EXPECT_EQ(dev_keys.keys, bob_req.device_keys.keys);
                  EXPECT_EQ(dev_keys.signatures, bob_req.device_keys.signatures);

                  responses += 1;
          });

        mtx::requests::QueryKeys bob_rk;
        bob_rk.device_keys[alice->user_id().to_string()] = {};
        bob->query_keys(
          bob_rk,
          [&responses, alice, alice_req](const mtx::responses::QueryKeys &res, ErrType err) {
                  check_error(err);

                  ASSERT_TRUE(res.failures.size() == 0);

                  auto bob_devices = res.device_keys.at(alice->user_id().to_string());
                  ASSERT_TRUE(bob_devices.size() > 0);

                  auto dev_keys = bob_devices.at(alice->device_id());
                  EXPECT_EQ(dev_keys.user_id, alice->user_id().to_string());
                  EXPECT_EQ(dev_keys.device_id, alice->device_id());
                  EXPECT_EQ(dev_keys.keys, alice_req.device_keys.keys);
                  EXPECT_EQ(dev_keys.signatures, alice_req.device_keys.signatures);

                  responses += 1;
          });

        while (responses != 2)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

        alice->close();
        bob->close();
}
