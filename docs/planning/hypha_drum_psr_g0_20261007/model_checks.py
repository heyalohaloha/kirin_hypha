#!/usr/bin/env python3
"""G0 independent, exact arithmetic prototype; never a Hypha implementation test.

Uses only synthetic values and injected clocks. Imports no product module, opens no
DAW/PCM/plugin_data, and writes only sibling model_measurements.json. Fraction is
the interval/clock oracle; Decimal is the display oracle. Expected values below
are explicit independent fixtures, not captured production outputs.
"""
from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal, ROUND_CEILING, ROUND_FLOOR, ROUND_HALF_EVEN, getcontext
from fractions import Fraction
from itertools import permutations
from pathlib import Path
import hashlib
import json
import sys

getcontext().prec = 1300
F = Fraction
D = Decimal


@dataclass(frozen=True)
class Endpoint:
    value: Fraction | None
    infinity: int = 0
    closed: bool = True

    def __post_init__(self):
        assert (self.value is not None) == (self.infinity == 0)
        assert self.infinity in (-1, 0, 1)
        assert self.infinity == 0 or not self.closed

    def order(self, lower):
        rank = self.infinity if self.infinity else 0
        tie = int(not self.closed) if lower else int(self.closed)
        return rank, self.value if self.value is not None else F(0), tie


NEG = Endpoint(None, -1, False)
POS = Endpoint(None, 1, False)


@dataclass(frozen=True)
class Interval:
    lower: Endpoint
    upper: Endpoint

    def __post_init__(self):
        assert self.lower.infinity != 1 and self.upper.infinity != -1
        low = self.lower.order(True)[:2]
        high = self.upper.order(False)[:2]
        assert low <= high
        assert low != high or (self.lower.closed and self.upper.closed)

    def is_point(self):
        return (self.lower.value is not None
                and self.lower.value == self.upper.value
                and self.lower.closed and self.upper.closed)

    def is_all_real(self):
        return self.lower.infinity == -1 and self.upper.infinity == 1

    def text(self):
        def value(e):
            return "-inf" if e.infinity == -1 else "+inf" if e.infinity else str(e.value)
        return (("[" if self.lower.closed else "(") + value(self.lower) + ","
                + value(self.upper) + ("]" if self.upper.closed else ")"))


def interval(low, high, low_closed=True, high_closed=True):
    return Interval(Endpoint(F(low), closed=low_closed),
                    Endpoint(F(high), closed=high_closed))


def point(value):
    return interval(value, value)


def lower_bound(value, closed=True):
    return Interval(Endpoint(F(value), closed=closed), POS)


REAL = Interval(NEG, POS)


def central_endpoint(values):
    middle = len(values) // 2
    if len(values) % 2:
        return values[middle]
    a, b = values[middle - 1:middle + 1]
    if a.infinity or b.infinity:
        infinity = a.infinity or b.infinity
        assert not (a.infinity and b.infinity and a.infinity != b.infinity)
        return NEG if infinity == -1 else POS
    return Endpoint((a.value + b.value) / 2, closed=a.closed and b.closed)


def interval_median(values):
    assert values
    lows = sorted((v.lower for v in values), key=lambda e: e.order(True))
    highs = sorted((v.upper for v in values), key=lambda e: e.order(False))
    return Interval(central_endpoint(lows), central_endpoint(highs))


def aggregate(values):
    """None is not-applicable; it never becomes an unrestricted latent value."""
    exact = [v for v in values if v is not None and v.is_point()]
    whole = interval_median(values) if values and all(v is not None for v in values) else None
    subset = interval_median(exact) if exact else None
    informative = whole is not None and not whole.is_all_real()
    kind = ("WholePoint" if whole.is_point() else "WholeInterval") if informative else (
        "ConfirmedSubset" if subset is not None else "NoScalar")
    return {
        "N": len(values), "exact": len(exact),
        "bound": sum(v is not None and not v.is_point() for v in values),
        "not_applicable": sum(v is None for v in values),
        "whole_available": whole is not None, "whole_informative": informative,
        "whole": whole.text() if whole else None,
        "subset": subset.text() if subset else None, "render_kind": kind,
    }


def scalar_decimal(value):
    if isinstance(value, Decimal):
        assert value.is_finite()
        return value
    return D(str(value))


def shared_display(low, high, nearest=False):
    """Returns exact Decimal glyph semantics; never casts a rounded value to float."""
    low, high = scalar_decimal(low), scalar_decimal(high)
    assert low <= high
    maximum = max(abs(low), abs(high))
    exponent = maximum.adjusted() if maximum else 0
    quantum = D("0.01")
    while True:
        scale = D(10) ** exponent
        lo = (low / scale).quantize(
            quantum, rounding=ROUND_HALF_EVEN if nearest else ROUND_FLOOR)
        hi = (high / scale).quantize(
            quantum, rounding=ROUND_HALF_EVEN if nearest else ROUND_CEILING)
        if max(abs(lo), abs(hi)) < 10:
            break
        exponent += 1
    shown_low, shown_high = lo * scale, hi * scale
    if not nearest:
        assert shown_low <= low and shown_high >= high
    return {
        "lower_mantissa": str(lo), "upper_mantissa": str(hi), "exponent": exponent,
        "contains_raw": shown_low <= low and shown_high >= high,
        "raw_lower": str(low), "raw_upper": str(high),
    }


class DisplayClock:
    """Injected milliseconds, rate=1 source millisecond / GUI millisecond."""
    def __init__(self, cutoff, lookbehind, time=F(0)):
        self.cutoff = F(cutoff)
        self.lookbehind = F(lookbehind)
        self.anchor_time, self.anchor_value = F(time), self.cutoff - self.lookbehind
        self.viewport = self.anchor_value
        self.held, self.anchors = False, 1

    def advance(self, time):
        if not self.held:
            proposed = self.anchor_value + F(time) - self.anchor_time
            self.viewport = min(proposed, self.cutoff)
            self.held = proposed >= self.cutoff
        assert self.viewport <= self.cutoff
        return self.viewport

    def publish(self, time, cutoff):
        self.advance(time)
        assert F(cutoff) >= self.cutoff
        self.cutoff = F(cutoff)
        if self.held:
            self.anchor_time = F(time)
            self.anchor_value = self.cutoff - self.lookbehind
            self.viewport = self.anchor_value
            self.held = False
            self.anchors += 1
        assert self.viewport <= self.cutoff


class CurrentValue:
    def __init__(self, source):
        self.source, self.available, self.value = source, True, None

    def apply(self, source, value):
        if not self.available or source != self.source:
            return False
        self.value = value
        return True

    def invalidate(self):
        self.available, self.value = False, None

    def begin(self, source):
        self.source, self.available, self.value = source, True, None


class SingleRequest:
    def __init__(self, token, accepted_ms, deadline_ms=1000):
        self.token = token
        self.deadline = F(accepted_ms) + deadline_ms
        self.state, self.value = "Acquiring", None

    def poll(self, time):
        if self.state == "Acquiring" and F(time) >= self.deadline:
            self.state, self.value = "Retired", None

    def reply(self, token, time, value):
        self.poll(time)
        if token != self.token or self.state != "Acquiring":
            return False
        self.state, self.value = "Full", value
        return True


def connects(previous, current):
    return previous != 0 and previous == current


def main():
    checks = []

    def check(name, measured, expected):
        passed = all(measured.get(k) == v for k, v in expected.items())
        checks.append({"id": name, "status": "PASS" if passed else "FAIL",
                       "expected": expected, "measured": measured})

    check("odd_point_with_bound", aggregate([point(0), point(10), lower_bound(10)]),
          {"N": 3, "exact": 2, "bound": 1, "whole": "[10,10]", "subset": "[5,5]",
           "render_kind": "WholePoint"})
    check("even_open_upper", {"median": interval_median(
        [interval(0, 1, high_closed=False), interval(2, 3)]).text()},
          {"median": "[1,2)"})
    check("even_open_lower", {"median": interval_median(
        [lower_bound(10, False), lower_bound(20)]).text()}, {"median": "(15,+inf)"})
    ties = [
        ("odd_lower_ties", [interval(0, 2), interval(0, 2, False), interval(0, 2, False)],
         "(0,2]"),
        ("odd_upper_ties", [interval(0, 2), interval(0, 2, True, False),
                           interval(0, 2, True, False)], "[0,2)"),
        ("even_lower_ties", [interval(0, 2), interval(0, 2),
                            interval(0, 2, False), interval(0, 2, False)], "(0,2]"),
    ]
    for name, values, expected in ties:
        results = {interval_median(list(p)).text() for p in permutations(values)}
        check(name, {"results": sorted(results), "permutations": len(list(permutations(values)))},
              {"results": [expected]})
    check("all_real_plus_two_points", aggregate([point(0), point(0), REAL]),
          {"whole_available": True, "whole_informative": True, "whole": "[0,0]",
           "bound": 1, "render_kind": "WholePoint"})
    check("all_real_no_scalar", aggregate([REAL] * 8),
          {"whole_available": True, "whole_informative": False,
           "whole": "(-inf,+inf)", "render_kind": "NoScalar"})
    check("all_real_explicit_subset", aggregate([point(-10)] + [REAL] * 7),
          {"N": 8, "exact": 1, "bound": 7, "whole": "(-inf,+inf)",
           "whole_informative": False, "subset": "[-10,-10]", "render_kind": "ConfirmedSubset"})
    check("D4_long_tail_dominates", aggregate([point(-10)] + [lower_bound(200)] * 7),
          {"whole": "[200,+inf)", "subset": "[-10,-10]", "render_kind": "WholeInterval"})
    check("not_applicable_not_all_real", aggregate([point(20), point(20), None]),
          {"N": 3, "not_applicable": 1, "whole_available": False,
           "subset": "[20,20]", "render_kind": "ConfirmedSubset"})
    check("shared_exponent_symmetric", shared_display("-3202.6", "3202.6"),
          {"lower_mantissa": "-3.21", "upper_mantissa": "3.21", "exponent": 3,
           "contains_raw": True})
    check("positive_lower_negative_upper", {
        "positive_lower": shared_display("3202.6", "3300")["lower_mantissa"],
        "negative_upper": shared_display("-3300", "-3202.6")["upper_mantissa"]},
          {"positive_lower": "3.20", "negative_upper": "-3.20"})
    check("nearest_point_separate", shared_display("3202.6", "3202.6", nearest=True),
          {"lower_mantissa": "3.20", "upper_mantissa": "3.20", "exponent": 3,
           "contains_raw": False})
    check("shared_exponent_carry", shared_display("-9999.1", "9999.1"),
          {"lower_mantissa": "-1.00", "upper_mantissa": "1.00", "exponent": 4,
           "contains_raw": True})
    ieee_max = D.from_float(sys.float_info.max)
    check("ieee_finite_max_outward", shared_display(-ieee_max, ieee_max),
          {"lower_mantissa": "-1.80", "upper_mantissa": "1.80", "exponent": 308,
           "contains_raw": True})
    ieee_positive = shared_display(ieee_max, ieee_max)
    check("ieee_max_one_sided_directions", ieee_positive,
          {"lower_mantissa": "1.79", "upper_mantissa": "1.80", "exponent": 308,
           "contains_raw": True})
    clock = DisplayClock(1000, 150)
    arrivals = {F(100): 1100, F(190): 1200, F(310): 1300}
    normal_times = sorted(set(F(i * 1000, 30) for i in range(13)) | set(arrivals))
    normal_trace = []
    for time in normal_times:
        if time in arrivals:
            clock.publish(time, arrivals[time])
        clock.advance(time)
        normal_trace.append((time, clock.viewport))
    slopes = {(b[1] - a[1]) / (b[0] - a[0])
              for a, b in zip(normal_trace, normal_trace[1:])}
    check("C100ms_V_continuous", {
        "slopes": sorted(str(x) for x in slopes), "anchors": clock.anchors,
        "V_at_400": str(clock.viewport), "C": str(clock.cutoff)},
          {"slopes": ["1"], "anchors": 1, "V_at_400": "1250", "C": "1300"})
    gap_trace = []
    for time in (440, 450, 600, 809):
        clock.advance(time)
        gap_trace.append({"t_ms": time, "V": str(clock.viewport),
                          "C": str(clock.cutoff), "HOLD": clock.held})
    clock.publish(810, 1800)  # 500 ms since the last publication at 310.
    recovery = {"V": str(clock.viewport), "C": str(clock.cutoff), "anchors": clock.anchors}
    clock.publish(900, 1900)
    clock.publish(1010, 2000)
    check("gap500_hold_single_return_anchor", {
        "gap_trace": gap_trace, "return": recovery, "final_anchors": clock.anchors,
        "all_V_le_C": all(F(x["V"]) <= F(x["C"]) for x in gap_trace)},
          {"return": {"V": "1650", "C": "1800", "anchors": 2},
           "final_anchors": 2, "all_V_le_C": True,
           "gap_trace": [{"t_ms": 440, "V": "1290", "C": "1300", "HOLD": False},
                         {"t_ms": 450, "V": "1300", "C": "1300", "HOLD": True},
                         {"t_ms": 600, "V": "1300", "C": "1300", "HOLD": True},
                         {"t_ms": 809, "V": "1300", "C": "1300", "HOLD": True}]})
    current = CurrentValue("source-A")
    current.apply("source-A", F("10.1"))
    current.invalidate()
    stale_accepted = current.apply("source-A", F("10.1"))
    after_invalid = current.value
    current.begin("source-B")
    current.apply("source-B", None)
    check("unavailable_source_no_finite_resurrection", {
        "stale_accepted": stale_accepted, "after_invalid": after_invalid,
        "new_source_none": current.value}, {
        "stale_accepted": False, "after_invalid": None, "new_source_none": None})
    request = SingleRequest("request-1", 0)
    request.poll(1000)
    accepted = request.reply("request-1", 1100, point(35))
    check("deadline_late_reply_rejected", {
        "state": request.state, "accepted": accepted, "value": request.value},
          {"state": "Retired", "accepted": False, "value": None})
    fresh = SingleRequest("request-2", 1200)
    stale = fresh.reply("request-1", 1300, point(35))
    good = fresh.reply("request-2", 2199, point(40))
    check("token_identity_and_before_deadline", {
        "stale": stale, "good": good, "state": fresh.state, "value": fresh.value.text()},
          {"stale": False, "good": True, "state": "Full", "value": "[40,40]"})
    check("mask_same_count_different_members", {
        "same_count": (1).bit_count() == (2).bit_count(),
        "connect_01_to_10": connects(1, 2), "connect_01_to_01": connects(1, 1),
        "connect_zero": connects(0, 0)},
          {"same_count": True, "connect_01_to_10": False,
           "connect_01_to_01": True, "connect_zero": False})
    report = {
        "schema": 1, "artifact": "G0 independent exact arithmetic prototype",
        "implementation_test": False, "product_modules_imported": 0,
        "scope": "Synthetic values and injected clocks only; no product/ABI/build/CI/hardware proof.",
        "clock_units": "exact milliseconds; illustrative lookbehind 150 ms",
        "decimal_point_tie_rule": "ROUND_HALF_EVEN in this prototype, separate from directed bounds",
        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "checks": len(checks), "pass": sum(x["status"] == "PASS" for x in checks),
        "fail": sum(x["status"] == "FAIL" for x in checks), "results": checks,
    }
    path = Path(__file__).with_name("model_measurements.json")
    # One JSON object per fixture keeps this evidence compact without losing typed inputs.
    prefix = {k: v for k, v in report.items() if k != "results"}
    lines = ["{"] + ["  " + json.dumps(k) + ": " + json.dumps(v, ensure_ascii=False) + ","
                     for k, v in prefix.items()]
    lines += ['  "results": [']
    lines += ["    " + json.dumps(x, ensure_ascii=False, allow_nan=False)
              + ("," if i + 1 < len(checks) else "") for i, x in enumerate(checks)]
    lines += ["  ]", "}"]
    path.write_text("\n".join(lines) + "\n")
    print(json.dumps({"pass": report["pass"], "fail": report["fail"],
                      "implementation_test": False, "output": path.name}))
    return 1 if report["fail"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
