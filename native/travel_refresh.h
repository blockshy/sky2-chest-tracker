// 传送菜单刷新请求的跨线程边界：输入线程只提交意图，原生线程负责实际重建。
// 本文件不持有游戏对象、不调用游戏函数，也不缓存任何需要恢复的点位状态。
#pragma once
#include <atomic>
#include <cstdint>

namespace tracker {

// 一次原生重建使用的不可变快照。调用方须保存整个值，并在完成或取消时原样交回。
// transaction 为零表示本次没有启动重建；它与 request 分开，避免上下文失效后
// 同一个开关请求的两次重建被误认为同一事务。
struct TravelRefreshTicket {
    uint64_t request = 0;
    uint64_t transaction = 0;
    uint64_t context = 0;

    explicit operator bool() const noexcept { return transaction != 0; }
    bool Enabled() const noexcept { return (request & 1u) != 0; }
};

// UI 只用这个快照显示目标状态、已生效状态及等待提示；不得据此操作游戏对象。
// 多个原子字段不构成锁住游戏线程的一致性事务。并发变化可能短暂显示前一帧状态或
// 等待提示，后续读取会自动追上；原生重建必须使用 TryBegin 返回的固定 ticket。
struct TravelRefreshStatus {
    uint64_t requestedTicket = 0;
    uint64_t appliedTicket = 0;
    bool requestedEnabled = false;
    bool appliedEnabled = false;
    bool pending = false;
    bool inProgress = false;
};

class TravelRefreshState {
public:
    // 每次真实按下沿增加一次序号，低位同时表示开／关。一次原子操作同时提交状态
    // 和顺序，不存在“先写 bool、后写 generation”导致原生线程读取到混合值的问题。
    // 初始序号为零：默认关闭，并视为当前原生菜单已经处于未修改状态。
    bool ToggleRequest() noexcept {
        const auto request = requested_.fetch_add(1, std::memory_order_acq_rel) + 1;
        return (request & 1u) != 0;
    }

    TravelRefreshStatus ReadStatus() const noexcept {
        const auto request = requested_.load(std::memory_order_acquire);
        const auto applied = applied_.load(std::memory_order_acquire);
        const bool valid = publishedValid_.load(std::memory_order_acquire);
        const bool busy = publishedBusy_.load(std::memory_order_acquire);
        return {request, applied, (request & 1u) != 0, (applied & 1u) != 0,
                request != applied || !valid || busy, busy};
    }

    // 以下方法只能由同一个原生更新线程调用，包括该线程中的同步重入调用。
    // safe 必须由调用方验证：处于允许重建的稳定界面，没有确认窗口或转场。
    // 不安全时连“同目标请求已确认”也不执行，保证请求留待真正安全的阶段处理。
    TravelRefreshTicket TryBegin(bool safe) noexcept {
        if (!safe || active_) return {};
        const auto request = requested_.load(std::memory_order_acquire);
        const auto applied = applied_.load(std::memory_order_relaxed);
        if (cacheValid_ && ((request ^ applied) & 1u) == 0) {
            // 两次切换在消费前回到原状态，不需要销毁并重建原生菜单。
            // 仅确认捕获的旧序号；并发新请求仍在 requested_ 中，不会被覆盖。
            if (request != applied) applied_.store(request, std::memory_order_release);
            return {};
        }
        ++transactionSerial_;
        // 零仅用于表示空 ticket；无符号回绕时跳过这个哨兵值。
        if (transactionSerial_ == 0) ++transactionSerial_;
        active_ = {request, transactionSerial_, context_};
        publishedBusy_.store(true, std::memory_order_release);
        return active_;
    }

    // 在原生脚本、候选修改、显示项与安全选中项均完成后调用。
    // 永远只确认开始时的 request：重建中收到的新请求会留在队列中，下一安全帧处理。
    // 旧上下文或旧事务不能提交到新菜单，也不能意外结束另一笔正在进行的事务。
    bool Complete(const TravelRefreshTicket& ticket) noexcept {
        if (!MatchesActive(ticket)) return false;
        active_ = {};
        if (ticket.context != context_) {
            publishedBusy_.store(false, std::memory_order_release);
            return false;
        }
        applied_.store(ticket.request, std::memory_order_release);
        cacheValid_ = true;
        publishedValid_.store(true, std::memory_order_release);
        publishedBusy_.store(false, std::memory_order_release);
        return true;
    }

    // 在调用方主动放弃或无法证明重建完整完成时调用。不能把可能部分重建的缓存
    // 当作旧状态仍有效，因此下一安全帧即使目标值没变，也会再次要求完整原生重算。
    bool Cancel(const TravelRefreshTicket& ticket) noexcept {
        if (!MatchesActive(ticket)) return false;
        active_ = {};
        InvalidateCache();
        publishedBusy_.store(false, std::memory_order_release);
        return true;
    }

    // 读档、菜单生命周期重建或其他已确认的原生上下文改变时调用。
    // 不靠内存地址辨别新旧存档，也不保存游戏指针。正在进行的旧事务保留 busy，
    // 防止同步重入再次开始刷新；待它返回后 Complete 会因上下文不匹配而拒绝提交。
    void ContextChanged() noexcept {
        ++context_;
        InvalidateCache();
    }

private:
    bool MatchesActive(const TravelRefreshTicket& ticket) const noexcept {
        return active_ && ticket.transaction == active_.transaction &&
               ticket.request == active_.request && ticket.context == active_.context;
    }

    void InvalidateCache() noexcept {
        cacheValid_ = false;
        publishedValid_.store(false, std::memory_order_release);
    }

    // 仅这四个字段跨线程发布；其余事务状态始终归原生更新线程独占。
    std::atomic<uint64_t> requested_{0};
    std::atomic<uint64_t> applied_{0};
    std::atomic<bool> publishedValid_{true};
    std::atomic<bool> publishedBusy_{false};
    uint64_t transactionSerial_ = 0;
    uint64_t context_ = 0;
    bool cacheValid_ = true;
    TravelRefreshTicket active_{};
};
}
