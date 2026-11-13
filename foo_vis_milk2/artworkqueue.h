// Track artwork hand-off; no UI objects or services are retained by workers.
// Language: en-GB. SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Milk2Artwork
{
inline constexpr size_t MaximumBytes = 64 * 1024 * 1024;
struct Image { std::vector<uint8_t> bytes; std::wstring path; };
struct FileJob { uint64_t generation; std::wstring path; };

class Queue
{
public:
    uint64_t BeginTrack(bool embedded)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_generation; m_embedded = embedded; m_job.reset();
        m_ready = Image{}; // Clear the previous track even when new art is absent.
        return m_generation;
    }
    void Open()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = false; ++m_generation; m_job.reset(); m_ready.reset();
    }
    void Close()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true; ++m_generation; m_job.reset(); m_ready.reset();
    }
    uint64_t EmbeddedGeneration()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_closed && m_embedded ? m_generation : 0;
    }
    bool IsCurrent(uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_closed && generation == m_generation;
    }
    bool Publish(uint64_t generation, Image image)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed || generation != m_generation || image.bytes.size() > MaximumBytes) return false;
        m_ready = std::move(image); return true;
    }
    bool QueueFile(uint64_t generation, std::wstring path)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed || generation != m_generation) return false;
        m_job = FileJob{generation, std::move(path)};
        if (m_workerActive) return false;
        m_workerActive = true; return true;
    }
    bool NextFile(FileJob& job)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed || !m_job) { m_workerActive = false; return false; }
        job = std::move(*m_job); m_job.reset(); return true;
    }
    void WorkerFailed()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_workerActive = false;
    }
    bool Take(Image& image)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed || !m_ready) return false;
        image = std::move(*m_ready); m_ready.reset(); return true;
    }
private:
    std::mutex m_mutex;
    uint64_t m_generation = 0;
    bool m_closed = false, m_embedded = true, m_workerActive = false;
    std::optional<FileJob> m_job;
    std::optional<Image> m_ready;
};
}
