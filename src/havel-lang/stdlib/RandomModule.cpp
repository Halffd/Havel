/* RandomModule.cpp - VM-native stdlib module */
#include "RandomModule.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

// ─── Shared RNG state ───────────────────────────────────────────
static std::mt19937 g_rng;
static bool g_seeded = false;
static std::string g_last_seed_info;

static void seedFromEntropy() {
  std::ifstream urandom("/dev/urandom", std::ios::binary);
  if (urandom) {
    std::seed_seq::result_type seed_data[std::mt19937::state_size];
    urandom.read(reinterpret_cast<char *>(seed_data), sizeof(seed_data));
    std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
    g_rng.seed(seq);
  } else {
    // Fallback: time-based
    g_rng.seed(
        static_cast<uint64_t>(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()));
  }
  g_seeded = true;
  g_last_seed_info = "entropy";
}

// ─── Value helpers ───────────────────────────────────────────────
static double toNum(const Value &v) {
  if (v.isInt())
    return static_cast<double>(v.asInt());
  if (v.isDouble())
    return v.asDouble();
  return 0.0;
}

static int64_t toInt(const Value &v) {
  if (v.isInt())
    return v.asInt();
  if (v.isDouble())
    return static_cast<int64_t>(v.asDouble());
  return 0;
}

// Register random module with VMApi (stable API layer)
void registerRandomModule(VMApi &api) {

  // Seed on first use from entropy source
  if (!g_seeded)
    seedFromEntropy();

  // ─── Basic random ─────────────────────────────────────────────
  api.registerFunction(
      "random.num", [](const std::vector<Value> &args) {
        if (args.empty()) {
          return Value::makeDouble(
              std::uniform_real_distribution<double>(0.0, 1.0)(g_rng));
        }
        if (args.size() == 1) {
          double max = toNum(args[0]);
          return Value::makeDouble(
              std::uniform_real_distribution<double>(0.0, max)(g_rng));
        }
        double lo = toNum(args[0]);
        double hi = toNum(args[1]);
        return Value::makeDouble(std::uniform_real_distribution<double>(lo, hi)(g_rng));
      });

  api.registerFunction(
      "random.int", [](const std::vector<Value> &args) {
        if (args.size() == 1) {
          int64_t mx = toInt(args[0]);
          return Value(
              static_cast<double>(
                  std::uniform_int_distribution<int64_t>(0, mx)(g_rng)));
        }
        int64_t lo = toInt(args[0]);
        int64_t hi = toInt(args[1]);
        return Value(
            static_cast<double>(
                std::uniform_int_distribution<int64_t>(lo, hi)(g_rng)));
      });

  api.registerFunction(
      "random.range", [](const std::vector<Value> &args) {
        if (args.empty()) {
          throw std::runtime_error("random.range() requires at least 1 "
                                   "argument");
        }
        if (args.size() == 1) {
          int64_t stop = toInt(args[0]);
          return Value(
              static_cast<double>(
                  std::uniform_int_distribution<int64_t>(0, stop - 1)(g_rng)));
        }
        int64_t start = toInt(args[0]);
        int64_t stop = toInt(args[1]);
        if (args.size() >= 3) {
          int64_t step = toInt(args[2]);
          if (step <= 0)
            throw std::runtime_error("random.range() step must be > 0");
          int64_t count = (stop - start + step - 1) / step;
          int64_t idx =
              std::uniform_int_distribution<int64_t>(0, count - 1)(g_rng);
          return Value(static_cast<double>(start + idx * step));
        }
        if (start >= stop)
          return Value(static_cast<double>(start));
        return Value(
            static_cast<double>(
                std::uniform_int_distribution<int64_t>(start, stop - 1)(
                    g_rng)));
      });

  // ─── Seeding ───────────────────────────────────────────────────
  api.registerFunction(
      "random.seed", [](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error("random.seed() requires 1 argument");
        const auto &v = args[0];
        if (v.isInt()) {
          g_rng.seed(static_cast<uint64_t>(v.asInt()));
          g_last_seed_info = "int:" + std::to_string(v.asInt());
        } else if (v.isDouble()) {
          g_rng.seed(static_cast<uint64_t>(v.asDouble()));
          g_last_seed_info =
              "double:" + std::to_string(static_cast<uint64_t>(v.asDouble()));
        } else if (v.isStringId()) {
          // Hash the string for seed
          std::hash<std::string> hasher;
          // Note: we'd need heap access for the actual string; fall back to a
          // simple numeric seed for now
          g_rng.seed(static_cast<uint64_t>(v.asStringId()));
          g_last_seed_info = "string_id:" + std::to_string(v.asStringId());
        } else if (v.isArrayId()) {
          // Use array id as seed material
          g_rng.seed(static_cast<uint64_t>(v.asArrayId()));
          g_last_seed_info = "array_id:" + std::to_string(v.asArrayId());
        } else {
          g_rng.seed(static_cast<uint64_t>(v.asInt()));
        }
        g_seeded = true;
        return Value::makeNull();
      });

  api.registerFunction("random.getSeed", [](const std::vector<Value> &) {
    return Value::makeNull();
  });

  // ─── Distributions ─────────────────────────────────────────────
  api.registerFunction(
      "random.normal", [](const std::vector<Value> &args) {
        double mean = 0.0, std = 1.0;
        if (args.size() >= 1)
          mean = toNum(args[0]);
        if (args.size() >= 2)
          std = toNum(args[1]);
        return Value(
            std::normal_distribution<double>(mean, std)(g_rng));
      });

  api.registerFunction(
      "random.exp", [](const std::vector<Value> &args) {
        double lambda = 1.0;
        if (args.size() >= 1)
          lambda = toNum(args[0]);
        return Value(std::exponential_distribution<double>(lambda)(g_rng));
      });

  api.registerFunction(
      "random.uniform", [](const std::vector<Value> &args) {
        double lo = 0.0, hi = 1.0;
        if (args.size() >= 1)
          lo = toNum(args[0]);
        if (args.size() >= 2)
          hi = toNum(args[1]);
        return Value(
            std::uniform_real_distribution<double>(lo, hi)(g_rng));
      });

  api.registerFunction(
      "random.gamma", [](const std::vector<Value> &args) {
        double shape = 1.0, scale = 1.0;
        if (args.size() >= 1)
          shape = toNum(args[0]);
        if (args.size() >= 2)
          scale = toNum(args[1]);
        return Value(
            std::gamma_distribution<double>(shape, scale)(g_rng));
      });

  api.registerFunction(
      "random.beta", [](const std::vector<Value> &args) -> Value {
        double a = 1.0, b = 1.0;
        if (args.size() >= 1)
          a = toNum(args[0]);
        if (args.size() >= 2)
          b = toNum(args[1]);
        // Beta via gamma: X ~ Gamma(a,1), Y ~ Gamma(b,1) => X/(X+Y) ~ Beta(a,b)
        std::gamma_distribution<double> ga(a, 1.0);
        std::gamma_distribution<double> gb(b, 1.0);
        double x = ga(g_rng);
        double y = gb(g_rng);
        return Value(x / (x + y));
      });

  api.registerFunction(
      "random.poisson", [](const std::vector<Value> &args) {
        double lambda = 1.0;
        if (args.size() >= 1)
          lambda = toNum(args[0]);
        return Value(
            static_cast<double>(
                std::poisson_distribution<int64_t>(lambda)(g_rng)));
      });

  // ─── Array operations ──────────────────────────────────────────
  api.registerFunction(
      "random.choice", [&api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isArrayId())
          throw std::runtime_error("random.choice() requires an array");
        size_t len = api.length(args[0]);
        if (len == 0)
          throw std::runtime_error("random.choice() requires non-empty array");
        size_t idx =
            std::uniform_int_distribution<size_t>(0, len - 1)(g_rng);
        return api.getAt(args[0], static_cast<uint32_t>(idx));
      });

  api.registerFunction(
      "random.choices", [&api](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId())
          throw std::runtime_error(
              "random.choices() requires array and k");
        size_t len = api.length(args[0]);
        int64_t k = toInt(args[1]);
        if (k < 0)
          throw std::runtime_error("random.choices() k must be >= 0");
        auto newArrId = api.makeArray();
        for (int64_t i = 0; i < k; ++i) {
          if (len > 0) {
            size_t idx =
                std::uniform_int_distribution<size_t>(0, len - 1)(
                    g_rng);
            api.push(newArrId, api.getAt(args[0], static_cast<uint32_t>(idx)));
          } else {
            api.push(newArrId, Value::makeNull());
          }
        }
        return newArrId;
      });

  api.registerFunction(
      "random.sample", [&api](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId())
          throw std::runtime_error(
              "random.sample() requires array and k");
        size_t len = api.length(args[0]);
        int64_t k = toInt(args[1]);
        if (k < 0 || static_cast<size_t>(k) > len)
          throw std::runtime_error(
              "random.sample() k out of range");
        // Fisher-Yates partial shuffle for sampling without replacement
        std::vector<size_t> indices(len);
        for (size_t i = 0; i < len; ++i)
          indices[i] = i;
        for (int64_t i = 0; i < k; ++i) {
          size_t j = std::uniform_int_distribution<size_t>(
              static_cast<size_t>(i), len - 1)(g_rng);
          std::swap(indices[i], indices[j]);
        }
        auto newArrId = api.makeArray();
        for (int64_t i = 0; i < k; ++i) {
          api.push(newArrId, api.getAt(args[0], static_cast<uint32_t>(indices[i])));
        }
        return newArrId;
      });

  api.registerFunction(
      "random.shuffle", [&api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isArrayId())
          throw std::runtime_error("random.shuffle() requires an array");
        size_t len = api.length(args[0]);
        if (len > 1) {
          // Read array into a vector, shuffle, write back
          std::vector<Value> elems;
          elems.reserve(len);
          for (size_t i = 0; i < len; ++i)
            elems.push_back(api.getAt(args[0], static_cast<uint32_t>(i)));
          std::shuffle(elems.begin(), elems.end(), g_rng);
          for (size_t i = 0; i < len; ++i)
            api.setAt(args[0], static_cast<uint32_t>(i), elems[i]);
        }
        return Value::makeNull();
      });

  api.registerFunction(
      "random.shuffled", [&api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isArrayId())
          throw std::runtime_error(
              "random.shuffled() requires an array");
        size_t len = api.length(args[0]);
        auto newArrId = api.makeArray();
        for (size_t i = 0; i < len; ++i)
          api.push(newArrId, api.getAt(args[0], static_cast<uint32_t>(i)));
        if (len > 1) {
          std::vector<Value> elems;
          elems.reserve(len);
          for (size_t i = 0; i < len; ++i)
            elems.push_back(api.getAt(newArrId, static_cast<uint32_t>(i)));
          std::shuffle(elems.begin(), elems.end(), g_rng);
          for (size_t i = 0; i < len; ++i)
            api.setAt(newArrId, static_cast<uint32_t>(i), elems[i]);
        }
        return newArrId;
      });

  api.registerFunction(
      "random.permutation", [&api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error(
              "random.permutation() requires n");
        int64_t n = toInt(args[0]);
        if (n < 0)
          throw std::runtime_error(
              "random.permutation() n must be >= 0");
        auto newArrId = api.makeArray();
        for (int64_t i = 0; i < n; ++i)
          api.push(newArrId, Value(static_cast<double>(i)));
        if (n > 1) {
          std::vector<Value> elems;
          elems.reserve(static_cast<size_t>(n));
          for (size_t i = 0; i < static_cast<size_t>(n); ++i)
            elems.push_back(api.getAt(newArrId, static_cast<uint32_t>(i)));
          std::shuffle(elems.begin(), elems.end(), g_rng);
          for (size_t i = 0; i < static_cast<size_t>(n); ++i)
            api.setAt(newArrId, static_cast<uint32_t>(i), elems[i]);
        }
        return newArrId;
      });

  // ─── Weighted random ───────────────────────────────────────────
  api.registerFunction(
      "random.weighted", [](const std::vector<Value> &args) -> Value {
        if (args.empty() || !args[0].isArrayId())
          throw std::runtime_error(
              "random.weighted() requires weights array");
        throw std::runtime_error(
            "random.weighted() not yet fully implemented - use "
            "random.weightedChoice");
      });

  api.registerFunction(
      "random.weightedChoice", [&api](const std::vector<Value> &args) {
        if (args.size() < 2 || !args[0].isArrayId() ||
            !args[1].isArrayId())
          throw std::runtime_error(
              "random.weightedChoice() requires array and weights");
        size_t alen = api.length(args[0]);
        size_t wlen = api.length(args[1]);
        if (alen == 0 || wlen == 0)
          throw std::runtime_error(
              "random.weightedChoice() requires non-empty arrays");
        if (alen != wlen)
          throw std::runtime_error(
              "random.weightedChoice() arrays must be same size");
        std::vector<double> weights;
        weights.reserve(wlen);
        double total = 0.0;
        for (size_t i = 0; i < wlen; ++i) {
          double wt = toNum(api.getAt(args[1], static_cast<uint32_t>(i)));
          weights.push_back(wt);
          total += wt;
        }
        if (total <= 0.0)
          throw std::runtime_error(
              "random.weightedChoice() weights must sum to > 0");
        double r = std::uniform_real_distribution<double>(0.0, total)(g_rng);
        double cum = 0.0;
        for (size_t i = 0; i < weights.size(); ++i) {
          cum += weights[i];
          if (r <= cum)
            return api.getAt(args[0], static_cast<uint32_t>(i));
        }
        return api.getAt(args[0], static_cast<uint32_t>(alen - 1));
      });

  // ─── Utility ───────────────────────────────────────────────────
  api.registerFunction(
      "random.bool", [](const std::vector<Value> &args) {
        double prob = 0.5;
        if (args.size() >= 1)
          prob = toNum(args[0]);
        return Value::makeBool(
            std::uniform_real_distribution<double>(0.0, 1.0)(g_rng) < prob);
      });

  api.registerFunction(
      "random.byte", [](const std::vector<Value> &) {
        return Value(
            static_cast<double>(
                std::uniform_int_distribution<int>(0, 255)(g_rng)));
      });

  api.registerFunction(
      "random.bytes", [&api](const std::vector<Value> &args) {
        if (args.empty())
          throw std::runtime_error(
              "random.bytes() requires count");
        int64_t n = toInt(args[0]);
        if (n < 0)
          throw std::runtime_error(
              "random.bytes() count must be >= 0");
        auto arrId = api.makeArray();
        std::uniform_int_distribution<int> d(0, 255);
        for (int64_t i = 0; i < n; ++i)
          api.push(arrId,
                   Value(static_cast<double>(d(g_rng))));
        return arrId;
      });

  // ─── Constants ─────────────────────────────────────────────────
  api.setGlobal("RAND_MAX",
                Value(static_cast<double>(std::mt19937::max())));

  // ─── Aliases (shorthand) ───────────────────────────────────────
  // These are set as globals in the script, not host functions
  // rand() -> random.num()
  // randint(min, max) -> random.int(min, max)
  // choice(arr) -> random.choice(arr)
  // We create wrapper lambdas that delegate to the main functions.

  // Note: For aliases we register them as host functions that call through
  api.registerFunction("rand", [](const std::vector<Value> &args) {
    return Value(std::uniform_real_distribution<double>(0.0, 1.0)(g_rng));
  });

  api.registerFunction(
      "randint", [](const std::vector<Value> &args) {
        if (args.size() == 1) {
          int64_t mx = toInt(args[0]);
          return Value(
              static_cast<double>(
                  std::uniform_int_distribution<int64_t>(0, mx)(g_rng)));
        }
        int64_t lo = toInt(args[0]);
        int64_t hi = toInt(args[1]);
        return Value(
            static_cast<double>(
                std::uniform_int_distribution<int64_t>(lo, hi)(g_rng)));
      });

  // ─── Build the random object ───────────────────────────────────
  auto randObj = api.makeObject();

  api.setField(randObj, "num", api.makeFunctionRef("random.num"));
  api.setField(randObj, "int", api.makeFunctionRef("random.int"));
  api.setField(randObj, "range", api.makeFunctionRef("random.range"));
  api.setField(randObj, "seed", api.makeFunctionRef("random.seed"));
  api.setField(randObj, "getSeed",
               api.makeFunctionRef("random.getSeed"));
  api.setField(randObj, "normal", api.makeFunctionRef("random.normal"));
  api.setField(randObj, "exp", api.makeFunctionRef("random.exp"));
  api.setField(randObj, "uniform", api.makeFunctionRef("random.uniform"));
  api.setField(randObj, "gamma", api.makeFunctionRef("random.gamma"));
  api.setField(randObj, "beta", api.makeFunctionRef("random.beta"));
  api.setField(randObj, "poisson", api.makeFunctionRef("random.poisson"));
  api.setField(randObj, "choice", api.makeFunctionRef("random.choice"));
  api.setField(randObj, "choices", api.makeFunctionRef("random.choices"));
  api.setField(randObj, "sample", api.makeFunctionRef("random.sample"));
  api.setField(randObj, "shuffle", api.makeFunctionRef("random.shuffle"));
  api.setField(randObj, "shuffled",
               api.makeFunctionRef("random.shuffled"));
  api.setField(randObj, "permutation",
               api.makeFunctionRef("random.permutation"));
  api.setField(randObj, "weighted",
               api.makeFunctionRef("random.weighted"));
  api.setField(randObj, "weightedChoice",
               api.makeFunctionRef("random.weightedChoice"));
  api.setField(randObj, "bool", api.makeFunctionRef("random.bool"));
  api.setField(randObj, "byte", api.makeFunctionRef("random.byte"));
  api.setField(randObj, "bytes", api.makeFunctionRef("random.bytes"));

  api.setGlobal("random", randObj);

  // Also register the convenience aliases as top-level globals
  api.setGlobal("rand", api.makeFunctionRef("rand"));
  api.setGlobal("randint", api.makeFunctionRef("randint"));
  api.setGlobal("choice", api.makeFunctionRef("random.choice"));
}

} // namespace havel::stdlib
