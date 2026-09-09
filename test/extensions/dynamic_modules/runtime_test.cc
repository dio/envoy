#include <array>
#include <limits>

#include "source/common/runtime/runtime_impl.h"
#include "source/extensions/dynamic_modules/runtime.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/status_utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

using Kind = envoy_dynamic_module_type_runtime_value_kind;
using Request = envoy_dynamic_module_type_runtime_request;
using Value = envoy_dynamic_module_type_runtime_value;
using Condition = envoy_dynamic_module_type_runtime_condition;
using Result = envoy_dynamic_module_type_runtime_read_result;
using testing::Return;
using testing::StrictMock;

class RuntimeBatchTest : public testing::Test {
protected:
  RuntimeBatchTest()
      : stats_{ALL_RUNTIME_STATS(POOL_COUNTER_PREFIX(store_, "runtime."),
                                 POOL_GAUGE_PREFIX(store_, "runtime."))} {}

  std::shared_ptr<Runtime::SnapshotImpl>
  snapshot(const absl::node_hash_map<std::string, std::string>& entries) {
    auto layer = std::make_unique<Runtime::AdminLayer>("test", stats_);
    EXPECT_OK(layer->mergeValues(entries));
    std::vector<Runtime::Snapshot::OverrideLayerConstPtr> layers;
    layers.push_back(std::move(layer));
    return std::make_shared<Runtime::SnapshotImpl>(random_, stats_, std::move(layers));
  }

  Request request(absl::string_view key,
                  Kind kind = Kind::envoy_dynamic_module_type_runtime_value_kind_String) {
    return {{key.data(), key.size()}, kind, true, 73, 1.25};
  }

  Result read(const std::vector<Request>& requests, const Condition* condition = nullptr,
              size_t capacity = 128) {
    return readRuntimeBatch(loader_, requests.data(), requests.size(), condition, values_.data(),
                            strings_.data(), capacity, &size_);
  }

  Stats::TestUtil::TestStore store_;
  Runtime::RuntimeStats stats_;
  StrictMock<Random::MockRandomGenerator> random_;
  StrictMock<Runtime::MockLoader> loader_;
  std::array<Value, ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_KEYS> values_{};
  std::array<char, 128> strings_{};
  size_t size_{0};
};

TEST_F(RuntimeBatchTest, NativeTypedValuesAndFallbacks) {
  auto current = snapshot({{"flag", "false"},
                           {"count", "42"},
                           {"ratio", "2.5"},
                           {"raw", std::string("a\0b", 3)},
                           {"invalid", "words"}});
  EXPECT_CALL(loader_, threadsafeSnapshot()).WillOnce(Return(current));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_Ok,
            read({request("flag", Kind::envoy_dynamic_module_type_runtime_value_kind_Boolean),
                  request("count", Kind::envoy_dynamic_module_type_runtime_value_kind_Integer),
                  request("ratio", Kind::envoy_dynamic_module_type_runtime_value_kind_Double),
                  request("raw"), request("missing"),
                  request("invalid", Kind::envoy_dynamic_module_type_runtime_value_kind_Integer),
                  request("missing", Kind::envoy_dynamic_module_type_runtime_value_kind_Boolean),
                  request("invalid", Kind::envoy_dynamic_module_type_runtime_value_kind_Double)}));
  EXPECT_FALSE(values_[0].boolean_value);
  EXPECT_EQ(42, values_[1].integer_value);
  EXPECT_EQ(2.5, values_[2].double_value);
  EXPECT_TRUE(values_[3].string_present);
  EXPECT_EQ(std::string("a\0b", 3), std::string(strings_.data(), size_));
  EXPECT_FALSE(values_[4].string_present);
  EXPECT_EQ(73, values_[5].integer_value);
  EXPECT_TRUE(values_[6].boolean_value);
  EXPECT_EQ(1.25, values_[7].double_value);
}

TEST_F(RuntimeBatchTest, BufferSizingDoesNotWritePartialValuesAndRetryReadsAgain) {
  auto first = snapshot({{"rev", "r1"}, {"payload", "old"}});
  auto second = snapshot({{"rev", "r2"}, {"payload", "new-data"}});
  EXPECT_CALL(loader_, threadsafeSnapshot()).WillOnce(Return(first)).WillOnce(Return(second));
  values_[0].integer_value = 999;
  strings_.fill('x');
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_BufferTooSmall,
            read({request("rev"), request("payload")}, nullptr, 1));
  EXPECT_EQ(5, size_);
  EXPECT_EQ(999, values_[0].integer_value);
  EXPECT_EQ('x', strings_[0]);
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_Ok,
            read({request("rev"), request("payload")}));
  first.reset();
  second.reset();
  EXPECT_EQ("r2new-data", std::string(strings_.data(), size_));
  EXPECT_EQ(2, values_[1].string_offset);
}

TEST_F(RuntimeBatchTest, ConditionSkipsPayloadAndMissingRevisionDoesNotMatch) {
  auto current = std::make_shared<StrictMock<Runtime::MockSnapshot>>();
  const std::string revision = "r1";
  EXPECT_CALL(*current, get("rev"))
      .WillOnce(Return(std::cref(revision)))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(*current, get("payload")).WillOnce(Return(std::nullopt));
  EXPECT_CALL(loader_, threadsafeSnapshot()).Times(2).WillRepeatedly(Return(current));
  Condition condition{{"rev", 3}, {"r1", 2}};
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_Unchanged,
            read({request("payload")}, &condition, 0));
  EXPECT_EQ(0, size_);
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_Ok,
            read({request("payload")}, &condition));
  EXPECT_FALSE(values_[0].string_present);
}

TEST_F(RuntimeBatchTest, EmptyStringIsPresentAndCanMatch) {
  auto current = std::make_shared<StrictMock<Runtime::MockSnapshot>>();
  const std::string empty;
  EXPECT_CALL(*current, get("empty")).Times(2).WillRepeatedly(Return(std::cref(empty)));
  EXPECT_CALL(loader_, threadsafeSnapshot()).Times(2).WillRepeatedly(Return(current));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_Ok, read({request("empty")}));
  EXPECT_TRUE(values_[0].string_present);
  EXPECT_EQ(0, values_[0].string_length);
  Condition condition{{"empty", 5}, {nullptr, 0}};
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_Unchanged, read({}, &condition));
}

TEST_F(RuntimeBatchTest, OneSnapshotIsRetainedWhenPublicationChangesDuringRead) {
  auto original = std::make_shared<StrictMock<Runtime::MockSnapshot>>();
  std::weak_ptr<Runtime::Snapshot> lifetime = original;
  Runtime::SnapshotConstSharedPtr published = original;
  const std::string revision = "old";
  const std::string payload = "old-payload";
  EXPECT_CALL(loader_, threadsafeSnapshot()).WillOnce([&] { return published; });
  EXPECT_CALL(*original, get("rev")).WillOnce([&](absl::string_view) {
    published = snapshot({{"rev", "new"}, {"payload", "new-payload"}});
    original.reset();
    EXPECT_FALSE(lifetime.expired());
    return std::optional<std::reference_wrapper<const std::string>>(std::cref(revision));
  });
  EXPECT_CALL(*original, get("payload")).WillOnce(Return(std::cref(payload)));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_Ok,
            read({request("rev"), request("payload")}));
  EXPECT_TRUE(lifetime.expired());
  EXPECT_EQ("oldold-payload", std::string(strings_.data(), size_));
}

TEST_F(RuntimeBatchTest, UnavailableSnapshot) {
  EXPECT_CALL(loader_, threadsafeSnapshot()).WillOnce(Return(nullptr));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_Unavailable,
            read({request("x")}));
  EXPECT_EQ(0, size_);
}

TEST_F(RuntimeBatchTest, RejectsInvalidInputBeforeAcquiringSnapshot) {
  auto bad = request("x");
  bad.key = {nullptr, 1};
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument, read({bad}));
  bad = request("x");
  bad.kind = static_cast<Kind>(99);
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument, read({bad}));
  for (auto kind : {Kind::envoy_dynamic_module_type_runtime_value_kind_String,
                    Kind::envoy_dynamic_module_type_runtime_value_kind_Integer,
                    Kind::envoy_dynamic_module_type_runtime_value_kind_Double}) {
    EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument,
              read({request("envoy.reloadable_features.test_feature_true", kind)}));
  }
  Condition condition{{nullptr, 0}, {nullptr, 0}};
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument,
            read({}, &condition));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument,
            readRuntimeBatch(loader_, nullptr, 1, nullptr, values_.data(), nullptr, 0, &size_));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument,
            readRuntimeBatch(loader_, nullptr, 0, nullptr, nullptr, nullptr, 1, &size_));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument,
            readRuntimeBatch(loader_, nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr));
}

TEST_F(RuntimeBatchTest, BoundsAndOverflow) {
  std::vector<Request> requests(65, request("x"));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_LimitExceeded, read(requests));
  auto oversized = request("x");
  oversized.key.length = std::numeric_limits<size_t>::max();
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_LimitExceeded, read({oversized}));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_LimitExceeded,
            read({}, nullptr, ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_OUTPUT_BYTES + 1));
  std::string key(ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_INPUT_BYTES, 'x');
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_LimitExceeded,
            read({request(key), request("x")}));
  auto current =
      snapshot({{"large", std::string(ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_OUTPUT_BYTES + 1, 'x')}});
  EXPECT_CALL(loader_, threadsafeSnapshot()).WillOnce(Return(current));
  EXPECT_EQ(Result::envoy_dynamic_module_type_runtime_read_result_LimitExceeded,
            read({request("large")}));
  EXPECT_EQ(0, size_);
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
