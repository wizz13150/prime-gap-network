#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

// The archive is deliberately lossless.  Rendering is bounded separately by
// FrontierCandidateProjection(), so old probable events never have to be
// destroyed merely to keep paint cost finite.
template <typename Candidate>
class FrontierLosslessCandidateArchive {
public:
    using container_type = std::vector<Candidate>;
    using value_type = Candidate;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;
    using reverse_iterator = typename container_type::reverse_iterator;
    using const_reverse_iterator = typename container_type::const_reverse_iterator;

    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    iterator begin() noexcept { return m_entries.begin(); }
    const_iterator begin() const noexcept { return m_entries.begin(); }
    const_iterator cbegin() const noexcept { return m_entries.cbegin(); }
    iterator end() noexcept { return m_entries.end(); }
    const_iterator end() const noexcept { return m_entries.end(); }
    const_iterator cend() const noexcept { return m_entries.cend(); }
    reverse_iterator rbegin() noexcept { return m_entries.rbegin(); }
    const_reverse_iterator rbegin() const noexcept { return m_entries.rbegin(); }
    const_reverse_iterator crbegin() const noexcept { return m_entries.crbegin(); }
    reverse_iterator rend() noexcept { return m_entries.rend(); }
    const_reverse_iterator rend() const noexcept { return m_entries.rend(); }
    const_reverse_iterator crend() const noexcept { return m_entries.crend(); }

    Candidate& operator[](std::size_t index) { return m_entries[index]; }
    const Candidate& operator[](std::size_t index) const { return m_entries[index]; }
    Candidate& back() { return m_entries.back(); }
    const Candidate& back() const { return m_entries.back(); }

    void append(const Candidate& candidate) { m_entries.push_back(candidate); }
    void append(Candidate&& candidate) { m_entries.push_back(std::move(candidate)); }
    iterator insert(iterator position, const Candidate& candidate)
    {
        return m_entries.insert(position, candidate);
    }

private:
    container_type m_entries;
};

struct FrontierCandidateProjectionRecord {
    std::size_t archiveIndex{0};
    bool exact{false};
    bool rejectedOrStale{false};
};

// Select every exact receipt plus a deterministic, chronology-wide sample of
// probable events.  The returned indices remain in archive order.  The
// probable budget bounds paint/hover work, while the archive above keeps the
// complete raw history available for counts, exports and future zoom levels.
inline std::vector<std::size_t> FrontierCandidateProjection(
    const std::vector<FrontierCandidateProjectionRecord>& eligible,
    std::size_t maximumProbable)
{
    std::vector<std::size_t> exact;
    std::vector<std::size_t> probable;
    exact.reserve(eligible.size());
    probable.reserve(eligible.size());
    for (const FrontierCandidateProjectionRecord& record : eligible) {
        (record.exact ? exact : probable).push_back(record.archiveIndex);
    }

    std::vector<std::size_t> selected = exact;
    if (probable.size() <= maximumProbable) {
        selected.insert(selected.end(), probable.begin(), probable.end());
    } else if (maximumProbable == 1) {
        selected.push_back(probable.back());
    } else if (maximumProbable > 1) {
        // Integer arithmetic selects both endpoints and evenly covers the
        // intervening chronology without floating-point rounding duplicates.
        for (std::size_t slot = 0; slot < maximumProbable; ++slot) {
            const std::size_t position =
                slot * (probable.size() - 1) / (maximumProbable - 1);
            selected.push_back(probable[position]);
        }
    }
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
}

// A graph projection is a rendering decision, not a data-retention decision.
// Keep the source archive lossless, then select deterministic representatives
// for the current viewport.  The bucket extrema preserve the visible shape of
// a distribution even when a million receipts land in the same pixel column.
struct FrontierGraphProjectionRecord {
    std::size_t archiveIndex{0};
    double x{0.0};
    double y{0.0};
    bool exact{false};
    // A terminal network verdict is causal state, not decoration.  The LOD
    // selector keeps at least one visible witness so a rejected/stale point
    // cannot disappear merely because a million-point archive was sampled.
    bool rejectedOrStale{false};
};

// Y-axis fitting follows the selected X viewport, not the complete retained
// archive.  Keeping this accumulator beside the lossless projection record
// lets the paint path derive the bounds while it already materializes records,
// without another archive scan at every repaint.
struct FrontierGraphVerticalBounds {
    bool valid{false};
    double minimum{0.0};
    double maximum{0.0};
};

inline void FrontierIncludeVisibleGraphPoint(FrontierGraphVerticalBounds& bounds,
                                             double minimumX,
                                             double maximumX,
                                             double x,
                                             double y)
{
    if (!std::isfinite(x) || !std::isfinite(y) || x < minimumX || x > maximumX) return;
    if (!bounds.valid) {
        bounds.valid = true;
        bounds.minimum = y;
        bounds.maximum = y;
        return;
    }
    bounds.minimum = std::min(bounds.minimum, y);
    bounds.maximum = std::max(bounds.maximum, y);
}

struct FrontierGraphProjectionResult {
    std::vector<std::size_t> archiveIndices;
    std::size_t visibleCount{0};
    bool sampled{false};
};

inline FrontierGraphProjectionResult FrontierViewportGraphProjection(
    const std::vector<FrontierGraphProjectionRecord>& records,
    double minimumX,
    double maximumX,
    double minimumY,
    double maximumY,
    int viewportPixels,
    std::size_t maximumPoints)
{
    FrontierGraphProjectionResult result;
    if (maximumX < minimumX || maximumY < minimumY || maximumPoints == 0) return result;
    // Two extrema per bucket are enough to retain sharp spikes and troughs;
    // use the oldest/newest representative only when a bucket is flat.  The
    // bucket count is tied to the viewport, so zooming changes the LOD rather
    // than merely hiding points after an expensive full paint.
    const std::size_t bucketCount = std::max<std::size_t>(
        1, std::min<std::size_t>(maximumPoints / 2,
                                 static_cast<std::size_t>(std::max(1, viewportPixels) * 2)));
    struct Bucket {
        std::size_t first{std::numeric_limits<std::size_t>::max()};
        std::size_t last{std::numeric_limits<std::size_t>::max()};
        std::size_t minimum{std::numeric_limits<std::size_t>::max()};
        std::size_t maximum{std::numeric_limits<std::size_t>::max()};
    };
    std::vector<Bucket> buckets(bucketCount);
    // Keep at most one direct frame.  The former implementation copied every
    // visible record before bucketing, so a one-million-point ALL viewport
    // paid a second million-record allocation on every source generation.
    std::vector<std::size_t> direct;
    direct.reserve(maximumPoints + 1);
    std::optional<std::size_t> terminalArchiveIndex;
    const double span = std::max(1.0e-12, maximumX - minimumX);
    for (std::size_t index = 0; index < records.size(); ++index) {
        const FrontierGraphProjectionRecord& record = records[index];
        if (!std::isfinite(record.x) || !std::isfinite(record.y) ||
            record.x < minimumX || record.x > maximumX ||
            record.y < minimumY || record.y > maximumY) {
            continue;
        }
        ++result.visibleCount;
        if (direct.size() <= maximumPoints) direct.push_back(record.archiveIndex);
        if (record.rejectedOrStale && !terminalArchiveIndex)
            terminalArchiveIndex = record.archiveIndex;
        const double ratio = std::clamp((record.x - minimumX) / span, 0.0, 1.0);
        const std::size_t bucket = std::min(bucketCount - 1,
                                            static_cast<std::size_t>(ratio * bucketCount));
        Bucket& slot = buckets[bucket];
        if (slot.first == std::numeric_limits<std::size_t>::max()) slot.first = index;
        slot.last = index;
        if (slot.minimum == std::numeric_limits<std::size_t>::max() ||
            record.y < records[slot.minimum].y) {
            slot.minimum = index;
        }
        if (slot.maximum == std::numeric_limits<std::size_t>::max() ||
            record.y > records[slot.maximum].y) {
            slot.maximum = index;
        }
    }
    if (result.visibleCount <= maximumPoints) {
        result.archiveIndices = std::move(direct);
        return result;
    }
    result.archiveIndices.reserve(std::min(maximumPoints, bucketCount * 4));
    std::unordered_set<std::size_t> selected;
    selected.reserve(std::min(maximumPoints, bucketCount * 4));
    const auto appendUnique = [&result, &records, &selected](std::size_t recordIndex) {
        if (recordIndex == std::numeric_limits<std::size_t>::max()) return;
        const std::size_t archiveIndex = records[recordIndex].archiveIndex;
        if (selected.insert(archiveIndex).second) {
            result.archiveIndices.push_back(archiveIndex);
        }
    };
    for (const Bucket& slot : buckets) {
        appendUnique(slot.first);
        appendUnique(slot.minimum);
        appendUnique(slot.maximum);
        appendUnique(slot.last);
    }
    std::sort(result.archiveIndices.begin(), result.archiveIndices.end());
    if (result.archiveIndices.size() > maximumPoints) {
        result.archiveIndices.resize(maximumPoints);
    }
    if (terminalArchiveIndex &&
        !std::binary_search(result.archiveIndices.cbegin(), result.archiveIndices.cend(),
                            *terminalArchiveIndex)) {
        if (result.archiveIndices.size() < maximumPoints) {
            result.archiveIndices.push_back(*terminalArchiveIndex);
        } else if (!result.archiveIndices.empty()) {
            result.archiveIndices.back() = *terminalArchiveIndex;
        }
        std::sort(result.archiveIndices.begin(), result.archiveIndices.end());
        result.archiveIndices.erase(std::unique(result.archiveIndices.begin(),
                                                result.archiveIndices.end()),
                                    result.archiveIndices.end());
    }
    result.sampled = true;
    return result;
}

// Projection is intentionally detached from the QWidget paint thread.  A
// new request replaces an older pending request, so a rapid pan/zoom gesture
// cannot build a queue of obsolete million-point scans.  The source records
// are moved into the worker and the caller decides how to publish the result
// (typically by posting a generation-checked callback to the UI thread).
class FrontierGraphProjectionWorker final {
public:
    using Callback = std::function<void(std::uint64_t, FrontierGraphProjectionResult)>;

    FrontierGraphProjectionWorker()
        : m_thread([this] { run(); })
    {
    }

    FrontierGraphProjectionWorker(const FrontierGraphProjectionWorker&) = delete;
    FrontierGraphProjectionWorker& operator=(const FrontierGraphProjectionWorker&) = delete;

    ~FrontierGraphProjectionWorker() { stop(); }

    void submit(std::vector<FrontierGraphProjectionRecord>&& records,
                double minimumX,
                double maximumX,
                double minimumY,
                double maximumY,
                int viewportPixels,
                std::size_t maximumPoints,
                std::uint64_t generation,
                Callback callback)
    {
        Task task;
        task.records = std::move(records);
        task.minimumX = minimumX;
        task.maximumX = maximumX;
        task.minimumY = minimumY;
        task.maximumY = maximumY;
        task.viewportPixels = viewportPixels;
        task.maximumPoints = maximumPoints;
        task.generation = generation;
        task.callback = std::move(callback);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) return;
            m_pending = std::move(task);
        }
        m_wakeup.notify_one();
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) {
                // The first caller owns the join.  A second call is safe and
                // avoids touching a non-joinable std::thread.
                if (!m_thread.joinable()) return;
            }
            m_stopping = true;
            m_pending.reset();
        }
        m_wakeup.notify_one();
        if (m_thread.joinable()) m_thread.join();
    }

private:
    struct Task {
        std::vector<FrontierGraphProjectionRecord> records;
        double minimumX{0.0};
        double maximumX{0.0};
        double minimumY{0.0};
        double maximumY{0.0};
        int viewportPixels{0};
        std::size_t maximumPoints{0};
        std::uint64_t generation{0};
        Callback callback;
    };

    void run()
    {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wakeup.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
                if (m_stopping && !m_pending.has_value()) return;
                task = std::move(*m_pending);
                m_pending.reset();
            }
            FrontierGraphProjectionResult result = FrontierViewportGraphProjection(
                task.records, task.minimumX, task.maximumX, task.minimumY,
                task.maximumY, task.viewportPixels, task.maximumPoints);
            if (task.callback) task.callback(task.generation, std::move(result));
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_wakeup;
    std::optional<Task> m_pending;
    bool m_stopping{false};
    std::thread m_thread;
};
