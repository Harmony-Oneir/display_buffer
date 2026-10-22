/*
 * oneir/display_buffer: замеры производительности аллокатора буферов.
 *
 * Работает как обычный клиент Display Buffer HDI (тот же путь, что у RS и Surface):
 *   AllocMem/FreeMem  — IPC в allocator_host → VDI → DRM (dumb buffers)
 *   Mmap/Unmap        — passthrough-mapper прямо в процессе клиента
 *
 * Сценарии:
 *   1. Выделение + освобождение буфера (задержка)
 *   2. Mmap + Unmap (задержка)
 *   3. Пропускная способность записи и чтения CPU через отображённую память
 *   4. FlushCache / InvalidateCache (задержка)
 *   5. «Кадровый цикл»: 3 буфера по кругу, mmap → заполнение → flush → unmap
 *
 * Запуск:  display_buffer_perf [-n итераций] [--strict]
 *   --strict — вернуть код 1, если превышены пороги (грубые ориентиры, см. CheckLimits)
 */
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "v1_0/include/idisplay_buffer.h"
#include "v1_0/display_composer_type.h"

using namespace OHOS::HDI::Display::Buffer::V1_0;
using namespace OHOS::HDI::Display::Composer::V1_0;

namespace {
using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Stats {
    double avg = 0;
    double p50 = 0;
    double p95 = 0;
    double max = 0;
};

Stats Calc(std::vector<double> v)
{
    Stats s;
    if (v.empty()) {
        return s;
    }
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v) {
        sum += x;
    }
    s.avg = sum / v.size();
    s.p50 = v[v.size() / 2];
    s.p95 = v[std::min(v.size() - 1, (v.size() * 95) / 100)];
    s.max = v.back();
    return s;
}

struct Scenario {
    const char *name;
    uint32_t width;
    uint32_t height;
};

const Scenario SCENARIOS[] = {
    {"720p", 1280, 720},
    {"1080p", 1920, 1080},
    {"4K", 3840, 2160},
};

AllocInfo MakeInfo(uint32_t w, uint32_t h)
{
    AllocInfo info = {};
    info.width = w;
    info.height = h;
    info.usage = HBM_USE_CPU_READ | HBM_USE_CPU_WRITE | HBM_USE_MEM_DMA;
    info.format = PIXEL_FMT_RGBA_8888;
    return info;
}

volatile uint64_t g_sink = 0; // чтобы компилятор не выбросил чтение

double GBps(size_t bytes, double ms)
{
    return ms > 0 ? (static_cast<double>(bytes) / 1e9) / (ms / 1e3) : 0;
}

struct Limits {
    double allocP95Ms = 50.0;   // выделение+освобождение одного буфера
    double mmapP95Ms = 5.0;     // mmap+unmap
    double writeMinGBps = 1.0;  // запись CPU в отображённый буфер
};

bool g_failed = false;

void CheckLimit(bool ok, const char *what)
{
    if (!ok) {
        printf("  !! превышен порог: %s\n", what);
        g_failed = true;
    }
}

template <typename Buffer>
void RunScenario(Buffer &buffer, const Scenario &sc, int iterations, const Limits &lim)
{
    printf("\n=== %s (%ux%u RGBA_8888) ===\n", sc.name, sc.width, sc.height);
    const AllocInfo info = MakeInfo(sc.width, sc.height);

    // 1. Выделение + освобождение
    std::vector<double> allocMs;
    for (int i = 0; i < iterations; ++i) {
        BufferHandle *h = nullptr;
        auto t0 = Clock::now();
        int32_t ret = buffer->AllocMem(info, h);
        if (ret != 0 || h == nullptr) {
            printf("  AllocMem failed: %d — сценарий пропущен\n", ret);
            return;
        }
        buffer->FreeMem(*h);
        allocMs.push_back(ElapsedMs(t0, Clock::now()));
    }

    // Один буфер для остальных замеров
    BufferHandle *h = nullptr;
    if (buffer->AllocMem(info, h) != 0 || h == nullptr) {
        printf("  AllocMem failed\n");
        return;
    }
    const size_t size = h->size;
    const unsigned stride = static_cast<unsigned>(h->stride);

    // 2. Mmap + Unmap
    std::vector<double> mapMs;
    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        void *p = buffer->Mmap(*h);
        if (p == nullptr) {
            printf("  Mmap failed\n");
            buffer->FreeMem(*h);
            return;
        }
        buffer->Unmap(*h);
        mapMs.push_back(ElapsedMs(t0, Clock::now()));
    }

    // 3. Пропускная способность CPU
    void *p = buffer->Mmap(*h);
    if (p == nullptr) {
        printf("  Mmap failed\n");
        buffer->FreeMem(*h);
        return;
    }
    std::vector<double> writeMs;
    std::vector<double> readMs;
    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        memset(p, i & 0xFF, size);
        writeMs.push_back(ElapsedMs(t0, Clock::now()));

        const uint64_t *words = static_cast<const uint64_t *>(p);
        uint64_t acc = 0;
        t0 = Clock::now();
        for (size_t w = 0; w < size / sizeof(uint64_t); ++w) {
            acc += words[w];
        }
        readMs.push_back(ElapsedMs(t0, Clock::now()));
        g_sink += acc;
    }

    // 4. Синхронизация кэшей
    std::vector<double> flushMs;
    std::vector<double> invMs;
    for (int i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        buffer->FlushCache(*h);
        flushMs.push_back(ElapsedMs(t0, Clock::now()));
        t0 = Clock::now();
        buffer->InvalidateCache(*h);
        invMs.push_back(ElapsedMs(t0, Clock::now()));
    }
    buffer->Unmap(*h);
    buffer->FreeMem(*h);

    // 5. Кадровый цикл: 3 буфера по кругу
    BufferHandle *ring[3] = {nullptr, nullptr, nullptr};
    bool ringOk = true;
    for (auto &r : ring) {
        ringOk = ringOk && buffer->AllocMem(info, r) == 0 && r != nullptr;
    }
    std::vector<double> frameMs;
    for (int i = 0; ringOk && i < iterations; ++i) {
        BufferHandle *b = ring[i % 3];
        auto t0 = Clock::now();
        void *fp = buffer->Mmap(*b);
        if (fp == nullptr) {
            ringOk = false;
            break;
        }
        memset(fp, i & 0xFF, b->size);
        buffer->FlushCache(*b);
        buffer->Unmap(*b);
        frameMs.push_back(ElapsedMs(t0, Clock::now()));
    }
    for (auto &r : ring) {
        if (r != nullptr) {
            buffer->FreeMem(*r);
        }
    }

    Stats a = Calc(allocMs);
    Stats m = Calc(mapMs);
    Stats w = Calc(writeMs);
    Stats r = Calc(readMs);
    Stats f = Calc(flushMs);
    Stats iv = Calc(invMs);
    Stats fr = Calc(frameMs);

    printf("  размер буфера: %.1f МБ, stride %u\n", size / (1024.0 * 1024.0), stride);
    printf("  %-26s %9s %9s %9s %9s\n", "операция", "avg, мс", "p50", "p95", "max");
    printf("  %-26s %9.3f %9.3f %9.3f %9.3f\n", "AllocMem+FreeMem", a.avg, a.p50, a.p95, a.max);
    printf("  %-26s %9.3f %9.3f %9.3f %9.3f\n", "Mmap+Unmap", m.avg, m.p50, m.p95, m.max);
    printf("  %-26s %9.3f %9.3f %9.3f %9.3f\n", "FlushCache", f.avg, f.p50, f.p95, f.max);
    printf("  %-26s %9.3f %9.3f %9.3f %9.3f\n", "InvalidateCache", iv.avg, iv.p50, iv.p95, iv.max);
    printf("  %-26s %9.3f %9.3f %9.3f %9.3f\n", "Кадр (map+fill+flush)", fr.avg, fr.p50, fr.p95, fr.max);
    printf("  запись CPU:  %.2f ГБ/с (по avg)\n", GBps(size, w.avg));
    printf("  чтение CPU:  %.2f ГБ/с (по avg)\n", GBps(size, r.avg));
    if (fr.avg > 0) {
        printf("  кадровый цикл: до %.0f кадров/с при заполнении CPU\n", 1000.0 / fr.avg);
    }

    CheckLimit(a.p95 <= lim.allocP95Ms, "AllocMem+FreeMem p95");
    CheckLimit(m.p95 <= lim.mmapP95Ms, "Mmap+Unmap p95");
    CheckLimit(GBps(size, w.avg) >= lim.writeMinGBps, "скорость записи CPU");
}
} // namespace

int main(int argc, char **argv)
{
    int iterations = 50;
    bool strict = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            iterations = std::max(1, atoi(argv[++i]));
        } else if (strcmp(argv[i], "--strict") == 0) {
            strict = true;
        }
    }

    auto buffer = IDisplayBuffer::Get();
    if (!buffer) {
        printf("IDisplayBuffer::Get() failed — запущен ли allocator_host?\n");
        return 2;
    }

    printf("display_buffer_perf: %d итераций на замер\n", iterations);
    Limits lim;
    for (const auto &sc : SCENARIOS) {
        RunScenario(buffer, sc, iterations, lim);
    }

    if (g_failed) {
        printf("\nИТОГ: есть превышения порогов%s\n", strict ? " (--strict → код 1)" : "");
        return strict ? 1 : 0;
    }
    printf("\nИТОГ: все пороги соблюдены\n");
    return 0;
}
