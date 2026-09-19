//! Channel layouts named by ITU role, never by index (D-5 / D-10).
//!
//! `ebur128`'s default channel map fixes everything past index 5 to `Unused`, so from seven
//! channels up a loudness sum silently loses channels. Every engine therefore takes its map from
//! here instead. A layout is recognised only by the exact set of roles it carries: an unknown set
//! is refused rather than guessed at (D-4), because neither channel count nor pair count
//! identifies a layout — 7.1 and 5.1.2 are both eight channels and both yield three pairs.
//!
//! Decisions: `docs/hypha_surround_decisions_20260918.md`.
//! ITU positions: `docs/hypha_surround_channel_map_findings_20260918.md` §4.

use ebur128::Channel;

/// The channel count the fixed-stereo path assumes.
///
/// `measure_thread` and `kirin_hypha_ffi` fall back to this for any host count other than 1 or 2,
/// so it is the stereo assumption itself, not a buffer size. It lives beside the layouts because
/// a layout, not a constant, is what replaces it as engines move onto `ChannelLayout`.
pub const N_CHANNELS: usize = 2;

/// How many channel slots the C ABI carries, and nothing more.
///
/// Capacity, not support: the widest layout Hypha recognises is 7.1.4 at twelve channels, and the
/// spare slots exist so that widening the ABI again is not what a new layout costs. A slot at or
/// past `channel_count` holds no measurement (D-3).
pub const MAX_ABI_CHANNELS: usize = 16;

/// Which revision of the role-to-`ebur128` mapping a measurement used.
///
/// Records carry it so two measurements are comparable only when they were weighted the same way.
/// Raise it whenever `loudness_channel` changes what a role is measured as — including if Hypha
/// ever moves from the weighting `vendor/ebur128` implements (BS.1770-4) to another (決定 §4.1).
/// The layout name alone cannot say this: "7.1.4" measured under two weightings is two things.
pub const MAPPING_REVISION: u32 = 1;

use ChannelRole as R;

/// One speaker position, by its ITU-R BS.2051 role.
///
/// The name is the identity. Two layouts with the same channel count hold different roles, and a
/// role that survives a layout change is the same measurement while an index is not.
///
/// The discriminants are the C ABI codes the shell passes to `kirin_hypha_create`. They are
/// written out so the enum itself is the wire format and `KirinChannelRole` in
/// `include/kirin_hypha_channels.h` has one thing to match. Never renumber a code in place.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum ChannelRole {
    /// M+000
    Centre = 0,
    /// M+030
    Left = 1,
    /// M-030
    Right = 2,
    /// Low frequency effects. Excluded from loudness, observed for peak and clip.
    Lfe = 3,
    /// M+110
    LeftSurround = 4,
    /// M-110
    RightSurround = 5,
    /// M+090
    LeftSurroundSide = 6,
    /// M-090
    RightSurroundSide = 7,
    /// M+135
    LeftSurroundRear = 8,
    /// M-135
    RightSurroundRear = 9,
    /// U+045
    TopFrontLeft = 10,
    /// U-045
    TopFrontRight = 11,
    /// U+135
    TopRearLeft = 12,
    /// U-135
    TopRearRight = 13,
}

/// Where a role sits, for the mirror rule. Azimuth is degrees, positive to the left.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Position {
    /// Listener-level plane at this azimuth.
    Middle(i16),
    /// Upper plane at this azimuth.
    Upper(i16),
    /// No position in the mirror sense.
    NoMirror,
}

impl ChannelRole {
    /// The role a C ABI code names, or `None` when the code names no role.
    ///
    /// An unknown code is refused, never clamped to a neighbour: a shell built against a newer
    /// header would otherwise have its extra channel silently measured as something else.
    pub fn from_abi(code: u8) -> Option<Self> {
        ALL_ROLES.get(code as usize).copied()
    }

    /// The C ABI code for this role.
    pub fn to_abi(self) -> u8 {
        self as u8
    }

    fn position(self) -> Position {
        match self {
            Self::Centre => Position::Middle(0),
            Self::Left => Position::Middle(30),
            Self::Right => Position::Middle(-30),
            Self::Lfe => Position::NoMirror,
            Self::LeftSurround => Position::Middle(110),
            Self::RightSurround => Position::Middle(-110),
            Self::LeftSurroundSide => Position::Middle(90),
            Self::RightSurroundSide => Position::Middle(-90),
            Self::LeftSurroundRear => Position::Middle(135),
            Self::RightSurroundRear => Position::Middle(-135),
            Self::TopFrontLeft => Position::Upper(45),
            Self::TopFrontRight => Position::Upper(-45),
            Self::TopRearLeft => Position::Upper(135),
            Self::TopRearRight => Position::Upper(-135),
        }
    }

    /// The role at the mirrored azimuth in the same plane.
    ///
    /// The median plane (0 and 180 degrees) mirrors onto itself, so it has none, and neither does
    /// LFE. This is the whole pair rule: no table of pairs is written anywhere (D-10).
    pub fn mirror(self) -> Option<Self> {
        let mirrored = match self.position() {
            Position::NoMirror => return None,
            Position::Middle(0) | Position::Middle(180) => return None,
            Position::Upper(0) | Position::Upper(180) => return None,
            Position::Middle(azimuth) => Position::Middle(-azimuth),
            Position::Upper(azimuth) => Position::Upper(-azimuth),
        };
        ALL_ROLES.iter().copied().find(|r| r.position() == mirrored)
    }

    /// True when this role is the left member of its pair, by ITU's positive-is-left convention.
    pub fn is_left_of_pair(self) -> bool {
        matches!(
            self.position(),
            Position::Middle(azimuth) | Position::Upper(azimuth) if azimuth > 0
        )
    }

    /// What `ebur128` must be told this channel is, so BS.1770 weighting reaches it.
    ///
    /// `Unused` here means "not part of the loudness sum", which is true of LFE alone. It never
    /// means "not measured": peak, clip and VU observe LFE like any other channel.
    pub fn loudness_channel(self) -> Channel {
        match self {
            Self::Centre => Channel::Center,
            Self::Left => Channel::Left,
            Self::Right => Channel::Right,
            Self::Lfe => Channel::Unused,
            Self::LeftSurround => Channel::LeftSurround,
            Self::RightSurround => Channel::RightSurround,
            Self::LeftSurroundSide => Channel::Mp090,
            Self::RightSurroundSide => Channel::Mm090,
            Self::LeftSurroundRear => Channel::Mp135,
            Self::RightSurroundRear => Channel::Mm135,
            Self::TopFrontLeft => Channel::Up045,
            Self::TopFrontRight => Channel::Um045,
            Self::TopRearLeft => Channel::Up135,
            Self::TopRearRight => Channel::Um135,
        }
    }

    /// The name of the `ebur128` channel this role is measured as, for records.
    ///
    /// A record says what was handed to the meter, not just what the layout was called: if the map
    /// were ever applied wrongly, the record is where it shows. `"unused"` is LFE's loudness
    /// exclusion, not a missing channel — its peak and clip are still observed.
    pub fn loudness_channel_name(self) -> &'static str {
        match self.loudness_channel() {
            Channel::Unused => "unused",
            Channel::Left => "Left",
            Channel::Right => "Right",
            Channel::Center => "Center",
            Channel::LeftSurround => "LeftSurround",
            Channel::RightSurround => "RightSurround",
            Channel::Mp090 => "Mp090",
            Channel::Mm090 => "Mm090",
            Channel::Mp135 => "Mp135",
            Channel::Mm135 => "Mm135",
            Channel::Up045 => "Up045",
            Channel::Um045 => "Um045",
            Channel::Up135 => "Up135",
            Channel::Um135 => "Um135",
            other => panic!("no record name for {other:?}; add it with the role that produced it"),
        }
    }

    /// Stable short name for records and displays. Never an index.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Centre => "C",
            Self::Left => "L",
            Self::Right => "R",
            Self::Lfe => "LFE",
            Self::LeftSurround => "Ls",
            Self::RightSurround => "Rs",
            Self::LeftSurroundSide => "Lss",
            Self::RightSurroundSide => "Rss",
            Self::LeftSurroundRear => "Lsr",
            Self::RightSurroundRear => "Rsr",
            Self::TopFrontLeft => "TFL",
            Self::TopFrontRight => "TFR",
            Self::TopRearLeft => "TRL",
            Self::TopRearRight => "TRR",
        }
    }
}

/// Every role, **indexed by its C ABI code**. The one place a code is bound to a role, and the
/// list `mirror` searches. Two tables would be two things to keep in step.
const ALL_ROLES: [ChannelRole; 14] = [
    ChannelRole::Centre,
    ChannelRole::Left,
    ChannelRole::Right,
    ChannelRole::Lfe,
    ChannelRole::LeftSurround,
    ChannelRole::RightSurround,
    ChannelRole::LeftSurroundSide,
    ChannelRole::RightSurroundSide,
    ChannelRole::LeftSurroundRear,
    ChannelRole::RightSurroundRear,
    ChannelRole::TopFrontLeft,
    ChannelRole::TopFrontRight,
    ChannelRole::TopRearLeft,
    ChannelRole::TopRearRight,
];

/// A left/right pair, derived rather than declared.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PairRole {
    pub left: ChannelRole,
    pub right: ChannelRole,
}

/// Which recognised layout this is. Carried in records so a change is visible (D-12).
///
/// The discriminants are the C ABI codes (`KirinChannelLayoutId`). 0 is reserved for "no layout
/// known", which is what a zeroed struct reads as, so a caller cannot mistake an uninitialised
/// field for mono.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LayoutId {
    Mono = 1,
    Stereo = 2,
    Surround5_0 = 3,
    Surround5_1 = 4,
    Surround7_1_4 = 5,
}

/// The ABI code for "this field holds no layout". Never a layout.
pub const LAYOUT_ID_UNKNOWN_ABI: u8 = 0;

/// Every layout Hypha recognises, in ABI code order.
pub const ALL_LAYOUT_IDS: [LayoutId; 5] = [
    LayoutId::Mono,
    LayoutId::Stereo,
    LayoutId::Surround5_0,
    LayoutId::Surround5_1,
    LayoutId::Surround7_1_4,
];

impl LayoutId {
    /// The C ABI code for this layout. Never `LAYOUT_ID_UNKNOWN_ABI`.
    pub fn to_abi(self) -> u8 {
        self as u8
    }

    /// The layout a C ABI code names, or `None` for 0 and for any code this build does not know.
    pub fn from_abi(code: u8) -> Option<Self> {
        ALL_LAYOUT_IDS
            .iter()
            .copied()
            .find(|id| id.to_abi() == code)
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Mono => "mono",
            Self::Stereo => "stereo",
            Self::Surround5_0 => "5.0",
            Self::Surround5_1 => "5.1",
            Self::Surround7_1_4 => "7.1.4",
        }
    }
}

/// A recognised layout: an ordered list of roles, in the order the host's buffer carries them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ChannelLayout {
    id: LayoutId,
    roles: &'static [ChannelRole],
}

/// Every layout Hypha recognises. Order matches the interleaved buffer, which for JUCE is the
/// ChannelType enum's ascending order, not the order a layout is usually written in: at 7.1.4 the
/// four ceiling channels come before the two rear surrounds.
const LAYOUTS: [ChannelLayout; 5] = [
    ChannelLayout {
        id: LayoutId::Mono,
        roles: &[R::Centre],
    },
    ChannelLayout {
        id: LayoutId::Stereo,
        roles: &[R::Left, R::Right],
    },
    ChannelLayout {
        id: LayoutId::Surround5_0,
        roles: &[
            R::Left,
            R::Right,
            R::Centre,
            R::LeftSurround,
            R::RightSurround,
        ],
    },
    ChannelLayout {
        id: LayoutId::Surround5_1,
        roles: &[
            R::Left,
            R::Right,
            R::Centre,
            R::Lfe,
            R::LeftSurround,
            R::RightSurround,
        ],
    },
    ChannelLayout {
        id: LayoutId::Surround7_1_4,
        roles: &[
            R::Left,
            R::Right,
            R::Centre,
            R::Lfe,
            R::LeftSurroundSide,
            R::RightSurroundSide,
            R::TopFrontLeft,
            R::TopFrontRight,
            R::TopRearLeft,
            R::TopRearRight,
            R::LeftSurroundRear,
            R::RightSurroundRear,
        ],
    },
];

/// Why a role list was not accepted. Every case is reported; none falls back to a guess.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LayoutError {
    /// No role at all.
    Empty,
    /// The same role appears twice.
    DuplicateRole(ChannelRole),
    /// The roles are valid but their combination is not a layout Hypha recognises.
    UnknownLayout,
    /// The roles match a layout, but not in the order its buffer carries them.
    WrongOrder(LayoutId),
}

impl ChannelLayout {
    /// Recognise a layout from the roles a host negotiated, in buffer order.
    ///
    /// Exact match only. A set of roles that resembles a known layout but is ordered differently,
    /// or holds an extra or missing role, is refused: measuring it would mean guessing which
    /// channel is which, and a wrong guess produces numbers that look right (D-4, D-13).
    pub fn recognise(roles: &[ChannelRole]) -> Result<Self, LayoutError> {
        if roles.is_empty() {
            return Err(LayoutError::Empty);
        }
        for (index, role) in roles.iter().enumerate() {
            if roles[..index].contains(role) {
                return Err(LayoutError::DuplicateRole(*role));
            }
        }
        if let Some(layout) = LAYOUTS.iter().find(|layout| layout.roles == roles) {
            return Ok(*layout);
        }
        // Same roles, different order: say so rather than reporting it as unknown, because the two
        // need different fixes.
        let mut sorted: Vec<ChannelRole> = roles.to_vec();
        sorted.sort();
        for layout in LAYOUTS.iter() {
            let mut known: Vec<ChannelRole> = layout.roles.to_vec();
            known.sort();
            if known == sorted {
                return Err(LayoutError::WrongOrder(layout.id));
            }
        }
        Err(LayoutError::UnknownLayout)
    }

    /// The single-channel layout.
    pub fn mono() -> Self {
        Self::by_id(LayoutId::Mono)
    }

    /// The two-channel layout.
    pub fn stereo() -> Self {
        Self::by_id(LayoutId::Stereo)
    }

    /// The layout a channel count names, for paths still clamped to mono or stereo.
    ///
    /// A count does not identify a layout — 7.1 and 5.1.2 are both eight channels — so this is
    /// total only where a `1..=2` guard already stands immediately above it. Anything else returns
    /// `None` rather than a layout. Each caller deletes its call when it takes a layout instead.
    pub fn mono_or_stereo_by_count(channels: usize) -> Option<Self> {
        match channels {
            1 => Some(Self::by_id(LayoutId::Mono)),
            2 => Some(Self::by_id(LayoutId::Stereo)),
            _ => None,
        }
    }

    /// Look a layout up by name, for state and records.
    pub fn by_id(id: LayoutId) -> Self {
        *LAYOUTS
            .iter()
            .find(|layout| layout.id == id)
            .expect("every LayoutId has a layout")
    }

    pub fn id(self) -> LayoutId {
        self.id
    }

    pub fn roles(self) -> &'static [ChannelRole] {
        self.roles
    }

    pub fn channel_count(self) -> usize {
        self.roles.len()
    }

    /// Where a role sits in the interleaved buffer, or `None` when the layout lacks it.
    pub fn index_of(self, role: ChannelRole) -> Option<usize> {
        self.roles.iter().position(|r| *r == role)
    }

    /// The map `ebur128` is given, in buffer order. Never the crate's default.
    pub fn loudness_map(self) -> Vec<Channel> {
        self.roles
            .iter()
            .map(|role| role.loudness_channel())
            .collect()
    }

    /// How many channels contribute to loudness. LFE does not.
    pub fn loudness_channel_count(self) -> usize {
        self.roles
            .iter()
            .filter(|role| role.loudness_channel() != Channel::Unused)
            .count()
    }

    /// Left/right pairs present in this layout, in buffer order of their left member.
    ///
    /// Derived from the mirror rule, so a layout Hypha has never seen cannot acquire a pair table
    /// by accident, and Correlation and Balance cannot end up looking at different pairs (D-10).
    pub fn pairs(self) -> Vec<PairRole> {
        let mut pairs = Vec::new();
        for role in self.roles {
            if !role.is_left_of_pair() {
                continue;
            }
            let Some(mirror) = role.mirror() else {
                continue;
            };
            if self.roles.contains(&mirror) {
                pairs.push(PairRole {
                    left: *role,
                    right: mirror,
                });
            }
        }
        pairs
    }
}

/// 役割で指す selector。layout の語彙そのものなのでここに置く。
#[path = "spectrum_view.rs"]
mod spectrum_view;
pub use spectrum_view::{SpectrumView, SPECTRUM_VIEW_ROLE_BASE};

#[cfg(test)]
#[path = "channel_layout_tests.rs"]
mod tests;
