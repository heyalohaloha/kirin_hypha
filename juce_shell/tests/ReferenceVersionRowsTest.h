#pragma once

#include "../src/HyphaReferenceVersionRows.h"

#include <cstdlib>
#include <iostream>

// V の Version の一覧（HyphaReferenceVersionRows.h）と、開いている一覧で押した行（SelectionControl）。
inline void verifyReferenceVersionRows()
{
    using namespace hypha::reference_ui;
    const auto check = [] (bool condition, const char* what)
    {
        if (condition) return;
        std::cerr << "Reference Version rows failed: " << what << '\n';
        std::exit (EXIT_FAILURE);
    };
    const auto rows = []
    {
        std::vector<SelectionOption> list;
        for (const auto& [id, label, work] : { std::tuple { "e1", "Song A v1", "song-a" }, std::tuple { "k1", "Song B v1", "song-b" },
                                               std::tuple { "e2", "Song A v2", "song-a" }, std::tuple { "s1", "Song C v1", "song-c" },
                                               std::tuple { "k2", "Song B v2", "song-b" } })
        {
            SelectionOption option { id, label };
            option.workId = work; option.versionId = juce::String (id) + "-version";
            list.push_back (option);
        }
        return list;
    };
    const auto order = [] (const std::vector<SelectionOption>& list)
    {
        juce::String text;
        for (const auto& row : list) text << row.id << (row.elsewhere ? "- " : " ");
        return text.trim();
    };

    auto undecided = rows();
    arrangeVersionRows (undecided, {}, {});
    check (order (undecided) == "e1 e2 k1 k2 s1", "no song known: every Version shows, song by song");

    auto chosen = rows();
    arrangeVersionRows (chosen, "e2", {});
    check (order (chosen) == "e1 e2 k1- k2- s1-", "the chosen Version's song shows, the others fold away");

    auto found = rows();
    arrangeVersionRows (found, {}, "k2");
    check (order (found) == "k1 k2 e1- e2- s1-", "with nothing chosen, AUTO tells the song");

    auto both = rows();
    arrangeVersionRows (both, "e1", "s1");
    check (order (both) == "e1 e2 s1 k1- k2-", "AUTO on another song stays in sight next to the chosen song");

    auto marked = rows();
    hypha::reference_audition::RuntimeAdoptedVersions adopted;
    adopted.versions.insert ({ "song-a", "e2-version" });
    adopted.versions.insert ({ "song-b", "k1-version" });
    markVersionRows (marked, "k1", &adopted);
    check (marked[0].label == "Song A v1", "an ordinary row keeps its name");
    check (marked[1].label == "Song B v1   ADOPTED / AUTO", "one row can carry both marks");
    check (marked[2].label == "Song A v2   ADOPTED", "the adopted Version says so");
    auto noStatus = rows();
    markVersionRows (noStatus, "k1", nullptr);
    check (noStatus[1].label == "Song B v1   AUTO" && noStatus[2].label == "Song A v2",
           "without the file from Kirin OS, only AUTO shows");

    // 開いている一覧の行の文字が変わっても（AUTO の印が動く）、押した行が選ばれる。無くなった行は何もしない。
    SelectionControl box;
    box.addItem ("v1", 1); box.addItem ("v2   AUTO", 2); box.addItem ("v3", 3); box.addItem ("Song B", 4);
    box.setRows ({ { "id-1", false }, { "id-2", false }, { "id-3", false }, { "id-4", true } });
    box.setElsewhereTitle (versionsElsewhereTitle);
    box.setSelectedId (1, juce::dontSendNotification);
    int changes = 0;
    box.onChange = [&changes] { ++changes; };
    box.changeItemText (2, "v2");
    box.changeItemText (3, "v3   AUTO");
    box.chooseRow ("id-3");
    check (box.getSelectedId() == 3 && changes == 1, "a row keeps its meaning while its text changes");
    box.chooseRow ("gone");
    check (box.getSelectedId() == 3 && changes == 1, "a row the list no longer has does nothing");

    const auto menu = box.makeMenu();
    int top = 0, folded = 0;
    juce::String foldTitle;
    for (juce::PopupMenu::MenuItemIterator item (menu); item.next();)
    {
        ++top;
        if (item.getItem().subMenu == nullptr) continue;
        foldTitle = item.getItem().text;
        for (juce::PopupMenu::MenuItemIterator inner (*item.getItem().subMenu); inner.next();) ++folded;
    }
    check (top == 4 && folded == 1 && foldTitle == "OTHER SONGS", "other songs fold into one row");
}
