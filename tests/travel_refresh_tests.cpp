// 传送刷新请求回归测试：只执行生产状态机，不加载游戏、不持有游戏对象或存档。
// 测试重点是输入与原生更新错开时的结果，避免只验证一次正常开关而漏掉旧请求覆盖。
#include "travel_refresh.h"
#include <cstdio>

namespace {
struct TestResult {
    unsigned checks = 0, failures = 0;
    void Check(bool value, const char* label) {
        ++checks;
        if (!value) { ++failures; std::printf("FAIL: %s\n", label); }
    }
};

void TestDefaultAndUnsafe(TestResult& result) {
    tracker::TravelRefreshState state;
    auto status = state.ReadStatus();
    result.Check(!status.requestedEnabled && !status.appliedEnabled && !status.pending,
                 "启动默认关闭且无需无意义重建");
    result.Check(!state.TryBegin(true), "初始稳定菜单不启动刷新");
    result.Check(state.ToggleRequest(), "第一次切换提交开启意图");
    result.Check(!state.TryBegin(false), "确认或转场等不安全阶段不开始刷新");
    status = state.ReadStatus();
    result.Check(status.requestedEnabled && !status.appliedEnabled && status.pending && !status.inProgress,
                 "不安全阶段保留目标与待处理状态，不能谎报已生效");
    const auto ticket = state.TryBegin(true);
    result.Check(ticket && ticket.Enabled(), "安全阶段获得固定开启快照");
    result.Check(!state.TryBegin(true), "原生同步重入不会开始第二笔刷新");
    result.Check(state.ReadStatus().inProgress, "重建期间向界面发布等待状态");
    result.Check(state.Complete(ticket), "完整刷新可以提交当前事务");
    status = state.ReadStatus();
    result.Check(status.appliedEnabled && !status.pending && !status.inProgress,
                 "提交完成后开启已生效且不再等待");
    result.Check(!state.Complete(ticket), "同一事务不能重复提交");
}

void TestCoalescing(TestResult& result) {
    tracker::TravelRefreshState state;
    state.ToggleRequest(); state.ToggleRequest();
    result.Check(!state.TryBegin(false) && state.ReadStatus().pending,
                 "双切换即使回到原值，不安全阶段也不提前确认");
    result.Check(!state.TryBegin(true), "开再关合并后不需要重建原本关闭的菜单");
    auto status = state.ReadStatus();
    result.Check(status.requestedTicket == 2 && status.appliedTicket == 2 && !status.pending,
                 "无须重建时仍确认最新已消费序号");
    state.ToggleRequest(); state.ToggleRequest(); state.ToggleRequest();
    const auto ticket = state.TryBegin(true);
    result.Check(ticket && ticket.request == 5 && ticket.Enabled(), "开关开合并为一次最终开启刷新");
    result.Check(state.Complete(ticket) && !state.TryBegin(true), "合并后的请求只刷新一次");
    state.ToggleRequest(); state.ToggleRequest();
    result.Check(!state.TryBegin(true), "已开启时关再开也不重建");
    status = state.ReadStatus();
    result.Check(status.appliedTicket == 7 && status.appliedEnabled && !status.pending,
                 "已开启同目标合并不丢请求序号");
}

void TestRequestDuringRebuild(TestResult& result) {
    tracker::TravelRefreshState state;
    state.ToggleRequest();
    const auto first = state.TryBegin(true);
    state.ToggleRequest();
    result.Check(first.Enabled() && !state.ReadStatus().requestedEnabled,
                 "新关闭请求不会改变已开始重建的开启快照");
    result.Check(!state.TryBegin(true), "新请求等前一事务结束再处理");
    result.Check(state.Complete(first), "第一事务只提交它捕获的开启状态");
    auto status = state.ReadStatus();
    result.Check(status.requestedTicket == 2 && status.appliedTicket == 1 &&
                 !status.requestedEnabled && status.appliedEnabled && status.pending,
                 "旧提交不会吞掉重建期间的新关闭请求");
    const auto second = state.TryBegin(true);
    result.Check(second && !second.Enabled(), "下一安全帧消费关闭请求");
    result.Check(!state.Complete(first) && !state.Cancel(first) && state.ReadStatus().inProgress,
                 "陈旧提交或取消不能结束新的事务");
    result.Check(state.Complete(second) && !state.ReadStatus().pending, "关闭最终生效且没有遗留请求");

    state.ToggleRequest();
    const auto third = state.TryBegin(true);
    state.ToggleRequest(); state.ToggleRequest();
    result.Check(state.Complete(third) && state.ReadStatus().pending,
                 "重建中两个新请求即使同目标，也保留未确认的序号");
    result.Check(!state.TryBegin(true) && !state.ReadStatus().pending,
                 "后续安全帧确认同目标新序号而不重复重建");
}

void TestContextAndCancellation(TestResult& result) {
    tracker::TravelRefreshState state;
    state.ContextChanged();
    result.Check(state.ReadStatus().pending && !state.TryBegin(false),
                 "新上下文即使目标关闭，也等待安全阶段重新计算");
    const auto first = state.TryBegin(true);
    result.Check(first && !first.Enabled(), "失效缓存不能通过同目标确认跳过重建");
    state.ContextChanged();
    result.Check(!state.TryBegin(true), "上下文在旧事务中改变时禁止重入开始新事务");
    result.Check(!state.Complete(first) && state.ReadStatus().pending && !state.ReadStatus().inProgress,
                 "旧上下文结果不提交，但返回后解除忙碌以便重试");
    const auto second = state.TryBegin(true);
    result.Check(second && second.request == first.request && second.context != first.context &&
                 second.transaction != first.transaction, "相同请求的新上下文获得独立事务身份");
    result.Check(!state.Complete(first) && state.ReadStatus().inProgress,
                 "旧上下文票据不能误提交相同请求的新事务");
    result.Check(state.Complete(second) && !state.ReadStatus().pending, "新上下文重建后恢复有效缓存");

    state.ToggleRequest();
    const auto interrupted = state.TryBegin(true);
    result.Check(state.Cancel(interrupted) && state.ReadStatus().pending, "取消保留请求并使部分重建缓存失效");
    state.ToggleRequest();
    const auto restore = state.TryBegin(true);
    result.Check(restore && !restore.Enabled(), "取消后改回已应用目标仍须完整重建，不能假设旧缓存未变");
    result.Check(!state.Cancel(interrupted) && state.ReadStatus().inProgress,
                 "旧取消票据不能影响恢复事务");
    result.Check(state.Complete(restore) && !state.ReadStatus().pending, "恢复事务完成后重新得到稳定关闭状态");

    const tracker::TravelRefreshTicket empty{};
    result.Check(!state.Complete(empty) && !state.Cancel(empty), "空票据不能提交或取消事务");
}
}

int main() {
    TestResult result;
    TestDefaultAndUnsafe(result);
    TestCoalescing(result);
    TestRequestDuringRebuild(result);
    TestContextAndCancellation(result);
    std::printf("%u travel refresh checks, %u failure(s)\n", result.checks, result.failures);
    return result.failures ? 1 : 0;
}
