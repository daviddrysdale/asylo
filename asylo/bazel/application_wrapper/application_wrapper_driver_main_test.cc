/*
 *
 * Copyright 2018 Asylo authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "asylo/bazel/application_wrapper/application_wrapper_driver_main.h"

#include <unistd.h>
#include <array>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/strings/str_cat.h"
#include "asylo/bazel/application_wrapper/application_wrapper.pb.h"
#include "asylo/bazel/application_wrapper/argv.h"
#include "asylo/client.h"
#include "asylo/enclave_manager.h"
#include "asylo/test/util/fake_enclave_loader.h"
#include "asylo/test/util/mock_enclave_client.h"
#include "asylo/test/util/proto_matchers.h"
#include "asylo/test/util/status_matchers.h"

namespace asylo {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

// A text fixture for the ApplicationWrapperDriverMain() tests.
class ApplicationWrapperDriverMainTest : public ::testing::Test {
 public:
  ApplicationWrapperDriverMainTest()
      : client_owned_(new StrictMock<MockEnclaveClient>) {
    client_ = client_owned_.get();
  }

  static void SetUpTestCase() {
    ASYLO_ASSERT_OK(EnclaveManager::Configure(EnclaveManagerOptions()));
  }

 protected:
  // Returns an EnclaveLoader that loads client_.
  FakeEnclaveLoader Loader() {
    return FakeEnclaveLoader(std::move(client_owned_));
  }

  // Destroys client_. Should only be used when ApplicationWrapperDriverMain()
  // does not destroy client_.
  void DestroyClient() {
    EnclaveManager *instance;
    ASYLO_ASSERT_OK_AND_ASSIGN(instance, EnclaveManager::Instance());

    EXPECT_CALL(*client_, EnterAndFinalize(_));
    EXPECT_CALL(*client_, DestroyEnclave());
    ASYLO_EXPECT_OK(instance->DestroyEnclave(client_, EnclaveFinal()));
  }

  // The EnclaveClient to use for testing.
  StrictMock<MockEnclaveClient> *client_;

 private:
  std::unique_ptr<StrictMock<MockEnclaveClient>> client_owned_;

  // Placing this here enforces the call sequence in all tests.
  InSequence enforce_call_order_;
};

// Tests that ApplicationWrapperDriverMain() invokes each of
// EnclaveManager::LoadEnclave(), client_.EnterAndRun(), and
// EnclaveManager::DestroyEnclave() on the created client once, in order.
TEST_F(ApplicationWrapperDriverMainTest,
       CallsLoadEnclaveThenEnterAndRunThenDestroyEnclave) {
  EnclaveOutput enclave_output;
  enclave_output.SetExtension(main_return_value, 0);

  EXPECT_CALL(*client_, EnterAndInitialize(_));
  EXPECT_CALL(*client_, EnterAndRun(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(enclave_output), Return(Status::OkStatus())));
  EXPECT_CALL(*client_, EnterAndFinalize(_));
  EXPECT_CALL(*client_, DestroyEnclave());

  char *arg = nullptr;
  EXPECT_THAT(ApplicationWrapperDriverMain(Loader(), "normal_workflow",
                                           /*argc=*/0, /*argv=*/&arg),
              IsOkAndHolds(0));
}

// Tests that ApplicationWrapperDriverMain() returns the same status as
// LoadEnclave() if the LoadEnclave() call fails.
TEST_F(ApplicationWrapperDriverMainTest, ForwardsFailureStatusFromLoadEnclave) {
  const Status load_enclave_failure(error::GoogleError::INTERNAL, "foobar");

  EXPECT_CALL(*client_, EnterAndInitialize(_))
      .WillOnce(Return(load_enclave_failure));
  EXPECT_CALL(*client_, EnterAndRun(_, _)).Times(0);
  EXPECT_CALL(*client_, EnterAndFinalize(_)).Times(0);
  EXPECT_CALL(*client_, DestroyEnclave());

  char *arg = nullptr;
  EXPECT_EQ(ApplicationWrapperDriverMain(Loader(), "load_failure", /*argc=*/0,
                                         /*argv=*/&arg)
                .status(),
            load_enclave_failure);
}

// Tests that ApplicationWrapperDriverMain() returns the same status as
// EnterAndRun() if the EnterAndRun() call fails.
TEST_F(ApplicationWrapperDriverMainTest, ForwardsFailureStatusFromEnterAndRun) {
  const Status enter_and_run_failure(error::GoogleError::INTERNAL, "foobar");

  // The enclave should still be destroyed even in EnterAndRun() fails.
  EXPECT_CALL(*client_, EnterAndInitialize(_));
  EXPECT_CALL(*client_, EnterAndRun(_, _))
      .WillOnce(Return(enter_and_run_failure));
  EXPECT_CALL(*client_, EnterAndFinalize(_));
  EXPECT_CALL(*client_, DestroyEnclave());

  char *arg = nullptr;
  EXPECT_EQ(ApplicationWrapperDriverMain(Loader(), "run_failure", /*argc=*/0,
                                         /*argv=*/&arg)
                .status(),
            enter_and_run_failure);
}

// Tests that ApplicationWrapperDriverMain() returns an error status if the
// EnclaveOutput from EnterAndRun() does not have a main_return_value extension.
TEST_F(ApplicationWrapperDriverMainTest,
       ReturnsErrorIfNoMainReturnValueExtension) {
  EXPECT_CALL(*client_, EnterAndInitialize(_));
  EXPECT_CALL(*client_, EnterAndRun(_, _));
  EXPECT_CALL(*client_, EnterAndFinalize(_));
  EXPECT_CALL(*client_, DestroyEnclave());

  char *arg = nullptr;
  EXPECT_THAT(
      ApplicationWrapperDriverMain(Loader(), "no_extension", /*argc=*/0,
                                   /*argv=*/&arg)
          .status(),
      StatusIs(error::GoogleError::INTERNAL,
               "EnclaveOutput does not have a main_return_value extension"));
}

// Tests that ApplicationWrapperDriverMain() returns the same status as
// DestroyEnclave() if the DestroyEnclave() call fails.
TEST_F(ApplicationWrapperDriverMainTest,
       ForwardsFailureStatusFromDestroyEnclave) {
  const Status destroy_enclave_failure(error::GoogleError::INTERNAL, "foobar");

  EnclaveOutput enclave_output;
  enclave_output.SetExtension(main_return_value, 0);

  EXPECT_CALL(*client_, EnterAndInitialize(_));
  EXPECT_CALL(*client_, EnterAndRun(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(enclave_output), Return(Status::OkStatus())));
  EXPECT_CALL(*client_, EnterAndFinalize(_))
      .WillOnce(Return(destroy_enclave_failure));
  EXPECT_CALL(*client_, DestroyEnclave()).Times(0);

  char *arg = nullptr;
  EXPECT_EQ(ApplicationWrapperDriverMain(Loader(), "destory_failure",
                                         /*argc=*/0, /*argv=*/&arg)
                .status(),
            destroy_enclave_failure);

  // Since the EnterAndFinalize() call fails, ApplicationWrapperDriverMain()
  // will not have destroyed the enclave.
  DestroyClient();
}

// Tests that ApplicationWrapperDriverMain() correctly propagates argv and argc
// to the EnclaveClient.
TEST_F(ApplicationWrapperDriverMainTest, PropagatesCommandLineArgs) {
  const std::array<std::string, 9> kTestArgs = {
      "the", "quick", "brown", "fox", "jumps", "over", "the", "lazy", "dog"};

  EnclaveConfig expected_config;
  CommandLineArgs *proto_args =
      expected_config.MutableExtension(command_line_args);
  for (const std::string &argument : kTestArgs) {
    proto_args->add_arguments(argument);
  }

  EnclaveOutput enclave_output;
  enclave_output.SetExtension(main_return_value, 0);

  EXPECT_CALL(*client_, EnterAndInitialize(Partially(expected_config)));
  EXPECT_CALL(*client_, EnterAndRun(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(enclave_output), Return(Status::OkStatus())));
  EXPECT_CALL(*client_, EnterAndFinalize(_));
  EXPECT_CALL(*client_, DestroyEnclave());

  Argv c_args(kTestArgs);
  int argc = c_args.argc();
  char **argv = c_args.argv();
  EXPECT_THAT(
      ApplicationWrapperDriverMain(Loader(), "command_line_args", argc, argv),
      IsOkAndHolds(0));
}

// Tests that ApplicationWrapperDriverMain() correctly propagates the
// main_return_value from the EnclaveClient.
TEST_F(ApplicationWrapperDriverMainTest, PropagatesMainReturnValue) {
  constexpr int kTestMainReturnValue = 42;

  EnclaveOutput enclave_output;
  enclave_output.SetExtension(main_return_value, kTestMainReturnValue);

  EXPECT_CALL(*client_, EnterAndInitialize(_));
  EXPECT_CALL(*client_, EnterAndRun(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(enclave_output), Return(Status::OkStatus())));
  EXPECT_CALL(*client_, EnterAndFinalize(_));
  EXPECT_CALL(*client_, DestroyEnclave());

  char *arg = nullptr;
  EXPECT_THAT(ApplicationWrapperDriverMain(Loader(), "main_return", /*argc=*/0,
                                           /*argv=*/&arg),
              IsOkAndHolds(kTestMainReturnValue));
}

}  // namespace
}  // namespace asylo