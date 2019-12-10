#
# Copyright 2019 Asylo authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//asylo/bazel:copts.bzl", "ASYLO_DEFAULT_COPTS")
load("//asylo/bazel:sgx_rules.bzl", "sgx_enclave_test")
load("@linux_sgx//:sgx_sdk.bzl", "sgx")

licenses(["notice"])

proto_library(
    name = "sgx_age_remote_assertion_authority_config_proto",
    srcs = ["sgx_age_remote_assertion_authority_config.proto"],
    visibility = ["//visibility:public"],
    deps = ["//asylo/crypto:certificate_proto"],
)

cc_proto_library(
    name = "sgx_age_remote_assertion_authority_config_cc_proto",
    visibility = ["//visibility:public"],
    deps = ["sgx_age_remote_assertion_authority_config_proto"],
)

proto_library(
    name = "sgx_age_remote_assertion_generator_test_enclave_proto",
    srcs = ["sgx_age_remote_assertion_generator_test_enclave.proto"],
    deps = [
        "//asylo:enclave_proto",
        "//asylo/crypto:certificate_proto",
        "//asylo/identity:identity_proto",
        "//asylo/identity/sgx:sgx_identity_proto",
    ],
)

cc_proto_library(
    name = "sgx_age_remote_assertion_generator_test_enclave_cc_proto",
    deps = ["sgx_age_remote_assertion_generator_test_enclave_proto"],
)

cc_library(
    name = "sgx_age_remote_assertion_generator",
    srcs = ["sgx_age_remote_assertion_generator.cc"],
    hdrs = ["sgx_age_remote_assertion_generator.h"],
    copts = ASYLO_DEFAULT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":sgx_age_remote_assertion_authority_config_cc_proto",
        "//asylo/crypto:certificate_cc_proto",
        "//asylo/crypto:certificate_util",
        "//asylo/grpc/auth:grpc++_security_enclave",
        "//asylo/grpc/auth:sgx_local_credentials_options",
        "//asylo/identity:identity_cc_proto",
        "//asylo/identity/attestation:enclave_assertion_generator",
        "//asylo/identity/sgx:code_identity_constants",
        "//asylo/identity/sgx:remote_assertion_cc_proto",
        "//asylo/identity/sgx:sgx_remote_assertion_generator_client",
        "//asylo/util:mutex_guarded",
        "//asylo/util:status",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
    ],
    alwayslink = 1,
)

sgx.unsigned_enclave(
    name = "sgx_age_remote_assertion_generator_test_enclave_unsigned.so",
    testonly = 1,
    srcs = ["sgx_age_remote_assertion_generator_test_enclave.cc"],
    copts = ASYLO_DEFAULT_COPTS,
    deps = [
        ":sgx_age_remote_assertion_generator",
        ":sgx_age_remote_assertion_generator_test_enclave_cc_proto",
        "//asylo:enclave_cc_proto",
        "//asylo:enclave_runtime",
        "//asylo/identity/sgx:sgx_identity_util",
        "//asylo/util:status_macros",
    ],
)

sgx.debug_enclave(
    name = "sgx_age_remote_assertion_generator_test_enclave.so",
    unsigned = "sgx_age_remote_assertion_generator_test_enclave_unsigned.so",
    config = "//asylo/grpc/util:grpc_enclave_config",
    testonly = 1,
)

sgx_enclave_test(
    name = "sgx_age_remote_assertion_generator_test",
    srcs = [
        "sgx_age_remote_assertion_generator.h",
        "sgx_age_remote_assertion_generator_test.cc",
    ],
    copts = ASYLO_DEFAULT_COPTS,
    enclaves = {
        "assertion_generator_enclave": "//asylo/identity/sgx:remote_assertion_generator_enclave.so",
        "sgx_age_remote_assertion_generator_test_enclave": ":sgx_age_remote_assertion_generator_test_enclave.so",
    },
    test_args = [
        "--assertion_generator_enclave_path='{assertion_generator_enclave}'",
        "--generator_test_enclave_path='{sgx_age_remote_assertion_generator_test_enclave}'",
    ],
    deps = [
        ":sgx_age_remote_assertion_authority_config_cc_proto",
        ":sgx_age_remote_assertion_generator",
        ":sgx_age_remote_assertion_generator_test_enclave_cc_proto",
        "//asylo:enclave_cc_proto",
        "//asylo:enclave_client",
        "//asylo/crypto:certificate_cc_proto",
        "//asylo/identity:descriptions",
        "//asylo/identity:enclave_assertion_authority",
        "//asylo/identity:identity_cc_proto",
        "//asylo/identity:init",
        "//asylo/identity/attestation:enclave_assertion_generator",
        "//asylo/identity/sgx:code_identity_constants",
        "//asylo/identity/sgx:remote_assertion_generator_enclave_cc_proto",
        "//asylo/identity/sgx:sgx_identity_cc_proto",
        "//asylo/identity/sgx:sgx_identity_util",
        "//asylo/identity/sgx:sgx_remote_assertion_generator_client",
        "//asylo/identity/sgx:sgx_remote_assertion_generator_impl",
        "//asylo/platform/primitives/sgx:loader_cc_proto",
        "//asylo/test/util:enclave_assertion_authority_configs",
        "//asylo/test/util:proto_matchers",
        "//asylo/test/util:status_matchers",
        "//asylo/test/util:test_main",
        "//asylo/util:mutex_guarded",
        "//asylo/util:status",
        "//asylo/util:thread",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
    ],
)