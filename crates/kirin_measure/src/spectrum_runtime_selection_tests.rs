use super::*;
use crate::channel_layout::{ChannelLayout, LayoutId};

/// Position, width, and label must come from the same captured selection.
#[test]
fn a_selection_names_the_view_it_actually_reads() {
    for layout in [
        ChannelLayout::mono(),
        ChannelLayout::stereo(),
        ChannelLayout::by_id(LayoutId::Surround5_1),
        ChannelLayout::by_id(LayoutId::Surround7_1_4),
    ] {
        let runtime = SpectrumRuntime::new(48_000, layout);
        for view in SpectrumView::available_in(layout) {
            assert!(runtime.set_view(view), "{view:?} in {:?}", layout.id());
            let selection = runtime
                .frame_selection(runtime.selection())
                .expect("a selection");

            assert_eq!(selection.input, layout.channel_count());
            assert_eq!(SpectrumView::from_abi(selection.view_code), Some(view));
            match view {
                SpectrumView::Channel(role) => {
                    assert_eq!(
                        selection.left,
                        layout.index_of(role).expect("role exists in layout"),
                        "{} reads its own position",
                        role.as_str()
                    );
                    assert_eq!(selection.right, None);
                }
                _ => {
                    assert_eq!(selection.left, 0);
                    assert_eq!(selection.right, (layout.channel_count() >= 2).then_some(1));
                }
            }
        }
    }
}
