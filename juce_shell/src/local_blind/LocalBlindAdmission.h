#pragma once
namespace hypha::local_blind
{
// Known refusal facts. A bool returned by a lower layer stays an unspecified admission failure.
enum class CaptureAdmission
{
    ready, unsupported, recovery, releasePending, pairRequired, keepBusy, referenceBusy,
    captureBusy, playbackRequired, clockUnavailable, engineUnavailable, admissionFailed, requestFailed,
    referenceLowered  // Reference が承認して A を下げている（RETURN が先。POST を二重に下げない）
};
}
