#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

// Network-difficulty observations only. No PRP difficulty, share target,
// chain length, gap merit, interpolation, or implicit timestamp conversion.
namespace HorizonDifficulty {
enum class Coin { Gapcoin, Riecoin, Primecoin };
enum class Clock : unsigned { BlockUtc = 1, ObservationUtc = 2 };
struct Sample {
    std::int64_t utcMs{};
    double value{};
    Clock clock{Clock::ObservationUtc};
};
struct Summary {
    std::int64_t firstUtcMs{}, lastUtcMs{};
    double minimum{}, maximum{};
    std::uint64_t timestamps{};
    unsigned clocks{};
    bool empty() const { return timestamps == 0; }
};
inline Summary merge(Summary a, const Summary& b)
{
    if (b.empty()) return a;
    if (a.empty()) return b;
    a.firstUtcMs = std::min(a.firstUtcMs, b.firstUtcMs);
    a.lastUtcMs = std::max(a.lastUtcMs, b.lastUtcMs);
    a.minimum = std::min(a.minimum, b.minimum);
    a.maximum = std::max(a.maximum, b.maximum);
    a.timestamps += b.timestamps;
    a.clocks |= b.clocks;
    return a;
}
struct Column {
    std::int64_t fromUtcMs{}, untilUtcMs{}; // half-open
    Summary samples;
};
struct Frame {
    std::int64_t fromUtcMs{}, untilUtcMs{};
    Summary retained, visible;
    std::vector<Column> columns;
};

// An augmented AVL tree rather than a selected-range scan. Historical batches
// may arrive out of UTC order. Insert and exact interval summaries are O(log N),
// including ALL: rendering is O(P log N), output/memory O(P), P <= 8192.
// Equal UTC timestamps retain BOTH extrema, making duplicate delivery idempotent
// without silently losing distinct blocks that happen to share a timestamp.
// A correction/reorg is a new source generation: clear/replay, never merge a
// corrected canonical chain into an old generation. This class owns no I/O.
class Index {
public:
    bool insert(const Sample& sample)
    {
        if (sample.utcMs <= 0 || sample.utcMs >= 253402300800000LL ||
            !std::isfinite(sample.value) || sample.value <= 0 ||
            (sample.clock != Clock::BlockUtc && sample.clock != Clock::ObservationUtc))
            return false;
        m_root = insertNode(std::move(m_root), sample);
        return true;
    }
    void clear() { m_root.reset(); }
    Summary retained() const { return summary(m_root); }
    Summary interval(std::int64_t from, std::int64_t until) const
    {
        return until > from ? intervalNode(m_root.get(), from, until) : Summary{};
    }
    Frame frame(std::int64_t from, std::int64_t until, int pixels) const
    {
        Frame result; result.fromUtcMs = from; result.untilUtcMs = until;
        result.retained = retained();
        if (until <= from || pixels <= 0) return result;
        pixels = std::clamp(pixels, 1, 8192);
        result.visible = interval(from, until);
        result.columns.reserve(std::size_t(pixels));
        const auto boundary = [=](int i) {
            return from + std::int64_t((static_cast<long double>(until) - from) * i / pixels);
        };
        for (int i = 0; i < pixels; ++i) {
            const auto left = boundary(i), right = boundary(i + 1);
            result.columns.push_back({left, right, interval(left, right)});
        }
        return result;
    }
private:
    struct Node {
        explicit Node(const Sample& sample)
            : own{sample.utcMs, sample.utcMs, sample.value, sample.value, 1,
                  static_cast<unsigned>(sample.clock)}, all(own) {}
        Summary own, all;
        int height{1};
        std::unique_ptr<Node> left, right;
    };
    static int height(const std::unique_ptr<Node>& n) { return n ? n->height : 0; }
    static Summary summary(const std::unique_ptr<Node>& n) { return n ? n->all : Summary{}; }
    static void refresh(Node& n)
    {
        n.height = 1 + std::max(height(n.left), height(n.right));
        n.all = merge(merge(summary(n.left), n.own), summary(n.right));
    }
    static std::unique_ptr<Node> leftRotate(std::unique_ptr<Node> n)
    {
        auto top = std::move(n->right);
        n->right = std::move(top->left); refresh(*n);
        top->left = std::move(n); refresh(*top); return top;
    }
    static std::unique_ptr<Node> rightRotate(std::unique_ptr<Node> n)
    {
        auto top = std::move(n->left);
        n->left = std::move(top->right); refresh(*n);
        top->right = std::move(n); refresh(*top); return top;
    }
    static std::unique_ptr<Node> insertNode(std::unique_ptr<Node> n, const Sample& sample)
    {
        if (!n) return std::make_unique<Node>(sample);
        if (sample.utcMs < n->own.firstUtcMs)
            n->left = insertNode(std::move(n->left), sample);
        else if (sample.utcMs > n->own.firstUtcMs)
            n->right = insertNode(std::move(n->right), sample);
        else {
            n->own.minimum = std::min(n->own.minimum, sample.value);
            n->own.maximum = std::max(n->own.maximum, sample.value);
            n->own.clocks |= static_cast<unsigned>(sample.clock);
        }
        refresh(*n);
        const int balance = height(n->left) - height(n->right);
        if (balance > 1) {
            if (height(n->left->left) < height(n->left->right))
                n->left = leftRotate(std::move(n->left));
            return rightRotate(std::move(n));
        }
        if (balance < -1) {
            if (height(n->right->right) < height(n->right->left))
                n->right = rightRotate(std::move(n->right));
            return leftRotate(std::move(n));
        }
        return n;
    }
    static Summary intervalNode(const Node* n, std::int64_t from, std::int64_t until)
    {
        if (!n || n->all.lastUtcMs < from || n->all.firstUtcMs >= until) return {};
        if (from <= n->all.firstUtcMs && n->all.lastUtcMs < until) return n->all;
        Summary result;
        if (n->own.firstUtcMs >= from && n->own.firstUtcMs < until) result = n->own;
        return merge(merge(intervalNode(n->left.get(), from, until), result),
                     intervalNode(n->right.get(), from, until));
    }
    std::unique_ptr<Node> m_root;
};
} // namespace HorizonDifficulty
