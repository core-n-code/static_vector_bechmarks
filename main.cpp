#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <print>
#include <random>
#include <ratio>
#include <utility>
#include <vector>

// prevent the compiler from optimizing away “useless” loads/stores:
template <class T> inline void DoNotOptimize(T const &value) {
  asm volatile("" : : "g"(value) : "memory");
}

// dummy type that is neither copyable nor movable
struct Dummy {
  Dummy() noexcept = default;
  Dummy(Dummy const &) = delete;
  Dummy(Dummy &&) = delete;
  std::size_t dummy = 0;
};

template <class T> class static_vector {
public:
  using value_type = T;
  constexpr static_vector() noexcept = delete;
  constexpr static_vector(const static_vector &) noexcept = delete;
  constexpr auto operator=(const static_vector &) noexcept
      -> static_vector & = delete;

  explicit static_vector(std::size_t capacity) noexcept : cap_(capacity) {
    data_ = static_cast<T *>(std::allocator<T>{}.allocate(capacity));
  }

  constexpr static_vector(static_vector &&other) noexcept
      : cap_(other.cap_), size_(std::exchange(other.size_, 0)),
        data_(std::exchange(other.data_, nullptr)) {}

  auto operator=(static_vector &&other) noexcept -> static_vector & {
    if (this != &other) {
      std::destroy_n(data_, size_);
      std::allocator<T>{}.deallocate(data_, cap_);
      cap_ = other.cap_;
      size_ = std::exchange(other.size_, 0);
      data_ = std::exchange(other.data_, nullptr);
    }
    return *this;
  }

  ~static_vector() noexcept {
    std::destroy_n(data_, size_);
    std::allocator<T>{}.deallocate(data_, cap_);
  }

  template <class... Args>
  constexpr auto emplace_back(Args &&...args) noexcept -> void {
    new (data_ + size_) T{std::forward<Args>(args)...};
    size_++;
  }

  template <class Self>
  constexpr auto operator[](this Self &&self, std::size_t idx) noexcept
      -> decltype(auto) {
    return std::forward<Self>(self).data_[idx];
  }

  constexpr auto begin() noexcept -> T * { return data_; }
  constexpr auto end() noexcept -> T * { return data_ + size_; }

  constexpr auto size() const noexcept -> std::size_t { return size_; }
  constexpr auto capacity() const noexcept -> std::size_t { return cap_; }

private:
  std::size_t cap_;
  std::size_t size_ = 0;
  T *data_ = nullptr;
};

struct Stats {
  double mean;
  double ci95;
};

auto clean_cache() {
  constexpr std::size_t bigger_than_cachesize = 17 * 1024 * 1024;
  static auto *p = new std::size_t[bigger_than_cachesize];

  static std::mt19937_64 rng{std::random_device()()};
  static std::uniform_int_distribution<std::size_t> dist{
      std::numeric_limits<std::size_t>::min(),
      std::numeric_limits<std::size_t>::max()};

  for (std::size_t i = 0; i < bigger_than_cachesize; i++) {
    p[i] = dist(rng);
  }
}

template <typename Fn>
auto measure(Fn fn, std::size_t trials) noexcept -> Stats {
  using namespace std::chrono;

  std::vector<double> samples;
  samples.reserve(trials);

  for (int i = 0; i < trials; ++i) {
    auto t0 = high_resolution_clock::now();
    fn();
    auto t1 = high_resolution_clock::now();

    clean_cache();

    samples.emplace_back(duration<double, std::micro>(t1 - t0).count());
  }
  const auto sum = std::accumulate(std::begin(samples), std::end(samples), 0.0);
  const auto mean = sum / trials;

  const auto var =
      std::accumulate(std::begin(samples), std::end(samples),
                      0.0,                       // initial sum
                      [mean](auto sum, auto s) { // lambda to accumulate
                        const auto diff = s - mean;
                        return sum + diff * diff;
                      });

  const auto stddev = std::sqrt(var / trials);
  const auto sem = stddev / std::sqrt(static_cast<double>(trials));
  const auto ci95 = 1.96 * sem;

  return {mean, ci95};
}

// Generate random indices
auto gen_indices(std::size_t N) noexcept -> std::vector<std::size_t> {
  std::mt19937_64 rng{std::random_device()()};
  std::uniform_int_distribution<std::size_t> dist{0, N - 1};
  std::vector<std::size_t> idx(N);

  for (auto &i : idx) {
    i = dist(rng);
  }

  return idx;
};

template <typename C> auto create_vec(std::size_t N) noexcept -> C {
  if constexpr (std::same_as<typename C::value_type, Dummy>) {
    C c(N);

    for (std::size_t i = 0; i < N; ++i) {
      c.emplace_back();
    }

    return c;
  } else {
    C c;
    c.reserve(N);

    for (std::size_t i = 0; i < N; ++i) {
      c.emplace_back(std::make_unique<Dummy>());
    }
    return c;
  }
}

template <typename C>
auto iterate(std::size_t N, int trials) noexcept -> Stats {

  auto c = create_vec<C>(N);

  return measure(
      [&] {
        for (auto const &x : c) {
          if constexpr (std::same_as<typename C::value_type, Dummy>) {
            DoNotOptimize(x.dummy);
          } else {
            DoNotOptimize(x->dummy);
          }
        }
      },
      trials);
}

template <typename C> auto access(std::size_t N, int trials) noexcept -> Stats {
  auto c = create_vec<C>(N);
  auto idx = gen_indices(N);

  return measure(
      [&] {
        for (auto i : idx) {
          if constexpr (std::same_as<typename C::value_type, Dummy>) {
            DoNotOptimize(c[i].dummy);
          } else {
            DoNotOptimize(c[i]->dummy);
          }
        }
      },
      trials);
}

template <typename C>
auto create_and_destruct(std::size_t N, int trials) noexcept -> Stats {
  return measure(
      [&] {
        if constexpr (std::same_as<C, static_vector<Dummy>>) {
          static_vector<Dummy> v(N);
          for (std::size_t i = 0; i < N; ++i) {
            v.emplace_back();
          }
          DoNotOptimize(v);
        } else {
          std::vector<std::unique_ptr<Dummy>> v;
          v.reserve(N);
          for (std::size_t i = 0; i < N; ++i) {
            v.emplace_back(std::make_unique<Dummy>());
          }
          DoNotOptimize(v);
        }
      },
      trials);
}

int main(int argc, char *argv[]) {
  // column sizes
  constexpr auto W1 = 12; // “Size”
  constexpr auto W2 = 22; // “Container”
  constexpr auto WM = 25; // each metric column

  const std::vector sizes = {1,       10,        100,        1'000,      10'000,
                             100'000, 1'000'000, 10'000'000, 100'000'000};

  const auto trials = 100;

  bool csv_output = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--csv") == 0) {
      csv_output = true;
      break;
    }
  }

  if (csv_output) {
    // CSV Header
    std::println("Size,Container,CreateDestroyMean,CreateDestroyCI95,"
                 "IterateMean,IterateCI95,AccessMean,AccessCI95");
  } else {
    // header
    std::println("{:<{}} {:<{}} {:>{}} {:>{}} {:>{}}", "Size", W1, "Container",
                 W2, "Create+Destroy (ms ±95%CI)", WM, "Iterate (ms ±95%CI)",
                 WM, "Access (ms ±95%CI)", WM);

    std::println("{:-<120}", "");
  }

  for (auto N : sizes) {
    const auto sv_c = create_and_destruct<static_vector<Dummy>>(N, trials);
    const auto sv_i = iterate<static_vector<Dummy>>(N, trials);
    const auto sv_a = access<static_vector<Dummy>>(N, trials);

    const auto vu_c =
        create_and_destruct<std::vector<std::unique_ptr<Dummy>>>(N, trials);
    const auto vu_i = iterate<std::vector<std::unique_ptr<Dummy>>>(N, trials);
    const auto vu_a = access<std::vector<std::unique_ptr<Dummy>>>(N, trials);

    if (csv_output) {
      // Output as CSV
      std::println("{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f}", N,
                   "static_vector", sv_c.mean, sv_c.ci95, sv_i.mean, sv_i.ci95,
                   sv_a.mean, sv_a.ci95);
      std::println("{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f}", N,
                   "vector<unique_ptr>", vu_c.mean, vu_c.ci95, vu_i.mean,
                   vu_i.ci95, vu_a.mean, vu_a.ci95);
    } else {
      // static_vector line
      std::println("{:<{}} {:<{}} {:>15.3f} ±{:>10.3f} {:>15.3f} ±{:>10.3f} "
                   "{:>15.3f} ±{:>10.3f}",
                   N, W1, "static_vector", W2, sv_c.mean, sv_c.ci95, sv_i.mean,
                   sv_i.ci95, sv_a.mean, sv_a.ci95);

      std::println("{:<{}} {:<{}} {:>15.3f} ±{:>10.3f} {:>15.3f} ±{:>10.3f} "
                   "{:>15.3f} ±{:>10.3f}",
                   N, W1, "vector<unique_ptr>", W2, vu_c.mean, vu_c.ci95,
                   vu_i.mean, vu_i.ci95, vu_a.mean, vu_a.ci95);
    }
  }
  return 0;
}
