#pragma once
#include "ReferenceRuntimeV2Model.h"

#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <memory>

namespace hypha::reference_audition
{
// Hypha が Kirin OS の library を読み終えたことを Kirin OS へ返す。library/received/<runtime_instance_id>.json を
// instance ごとに 1 つ上書きする（Kirin OS は読むだけ）。Audio Thread では書かない。
// observe は作業スレッドから呼び、自分のファイルの有無を見るほかは I/O を待たない。保存と掃除は別の thread で 1 本だけ
// 走らせ、走っている間は次の内容を 1 つだけ覚える（C の MATCH と再生の準備を遅らせない）。
// 読み終えた revision と外した項目が前回と同じなら書かない。sets.json は manifest の後に追いつくので、同じ revision でも
// 外した項目が変われば書く。作り直しの後に読んだ低い revision では戻さない。別の instance の掃除が消した知らせは次の周で
// 同じ内容に戻す。書けなくても Reference は止めず、知らせず、次の内容でまた試す。
class LibraryReceivedWriter final
{
public:
    using Store = std::function<bool (const juce::File&, const juce::String&)>;  // 空なら atomic に書く
    static constexpr std::int64_t maximumBytes = 64 * 1024;
    static constexpr int maximumRows = 256;  // 超える分は切る（maximumBytes に収まらない行も切る）
    // id は instance ごとに新しく、ファイルは残り続ける。書くときに、これより古い同じ形の名前のファイルを消す。
    static constexpr std::int64_t lifetimeMs = 30LL * 24 * 60 * 60 * 1000;
    static constexpr int maximumScannedFiles = 256;  // 1 回の掃除で見るファイルの数

    explicit LibraryReceivedWriter (juce::File transportRoot, Store = {});
    ~LibraryReceivedWriter();  // 入力はジョブが所有し、コードは host 終了まで保持。破棄側は I/O を待たない
    void observe (const RuntimeIdentity&, const RuntimeWorkspace&, std::int64_t nowMs);
    void service (std::int64_t nowMs);  // 選択準備とは独立して毎周期、結果回収・保留送信・消失確認を行う
    void stop() noexcept;  // 待たない
    void finish();         // 試験でだけ、走っている 1 回を待つ。observe と同じスレッドから

private:
    void startWrite (std::int64_t nowMs);

    const juce::File folder;
    const Store store;
    const std::shared_ptr<std::atomic<bool>> stopped = std::make_shared<std::atomic<bool>> (false);
    std::map<juce::String, std::int64_t> highestRevisions;  // instance ごと
    juce::String latestId, latestText;
    std::int64_t latestRevision = 0;
    std::vector<RuntimeSkippedItem> latestSkipped;
    bool writePending = false;  // 今の内容がまだ走っていない
    bool latestStored = false;  // 今の内容を書けた（消えていたら書き直す）
    std::shared_future<bool> running;  // packaged_task の結果。最後の参照を捨てても I/O を待たない
};
}
