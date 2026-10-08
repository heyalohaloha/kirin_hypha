#include "HyphaObservatoryView.h"

namespace hypha::observatory
{
bool View::setTimeSnapshot (const KirinTimeSnapshotV2& packet,
                            std::vector<KirinTimeHistoryEntryV2> main,
                            std::vector<KirinTimeHistoryEntryV2> psr,
                            double pollStartedMs, double nowMs, bool showPsr)
{
    if (! timePresentation.apply (packet, std::move (main), std::move (psr),
                                  pollStartedMs, nowMs, showPsr))
        return false;
    repaint (bodyArea);
    return true;
}

void View::advanceTimePresentation (double nowMs)
{
    if (timePresentation.advance (nowMs)) repaint (bodyArea);
}

void View::retireTimePresentation (bool localSourceChanged)
{
    timePresentation.retire (localSourceChanged);
    repaint (bodyArea);
}

}
