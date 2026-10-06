#pragma once

#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
    class RuntimeV2Repository final
    {
    public:
        explicit RuntimeV2Repository (juce::File transportRootIn = transportRoot());

        RuntimeWorkspaceLoadResult refresh (
            const juce::String& workId,
            std::shared_ptr<const RuntimeWorkspace> previous = {}) const;

        RuntimeWorkspaceLoadResult refreshLibrary (std::shared_ptr<const RuntimeWorkspace> previous = {}) const;
        bool libraryOnline (std::int64_t nowMs) const;
        // Kirin OS の準備の状態。期限の過ぎたもの（Kirin OS が閉じている）・形の違うものは無し。読めない曲は飛ばす。
        std::shared_ptr<const RuntimeLibraryPreparation> libraryPreparation (std::int64_t nowMs) const;

        static juce::File transportRoot();
        // 試験だけが呼ぶ（製品は呼ばない）：Reference の置き場所を試験用のフォルダへ向ける。JUCE のホームは macOS で
        // 環境変数 HOME に従わないので、HOME を変えるだけでは本物の Kirin OS の場所を読み書きしてしまう。空で戻す。
        static void setTransportRootForTesting (const juce::File&);

    private:
        const juce::File root;
    };
}
