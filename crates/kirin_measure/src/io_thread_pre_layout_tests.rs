//! PRE が「どの map で測ったか」を pre.json に名乗ること（B-976 / Gate A1）。

use super::*;


    /// **書き手と読み手が本当に繋がっているかを確かめる。**
    ///
    /// POST 側の試験は `pre.json` を手で組み立てる。それだけだと、PRE が実際には
    /// 何も書いていなくても POST 側は「常に LayoutUnknown」で自己整合してしまう。
    /// ここで出荷経路の writer が実際に何を書くかを見る（B-976 / Gate A1）。
    #[test]
    fn the_shipping_writer_states_the_layout_it_measured() {
        let dir = std::env::temp_dir().join(format!(
            "kirin_pre_layout_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&dir).unwrap();
        let file = dir.join("pre.json");
        let result = Arc::new(Mutex::new(MeasureResult {
            lufs_m: Some(-14.0),
            ..MeasureResult::default()
        }));
        let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));

        for (layout, expected_name, expected_channels) in [
            (crate::channel_layout::ChannelLayout::mono(), "mono", 1),
            (crate::channel_layout::ChannelLayout::stereo(), "stereo", 2),
        ] {
            write_json(
                &file,
                "pre-instance",
                "Mix",
                "daw",
                "owner",
                &result,
                &signal_state,
                layout,
            )
            .unwrap();
            let text = std::fs::read_to_string(&file).unwrap();
            let parsed: serde_json::Value = serde_json::from_str(&text).unwrap();

            // transport は据え置き。旧 POST も読める。
            assert_eq!(parsed["v"], 2, "v は上げない");

            let stated: crate::plugin_data::MeasurementLayout =
                serde_json::from_value(parsed["layout"].clone())
                    .expect("writer states a readable layout");
            // 期待値はリテラル。製品の `ChannelLayout` から作らない（試験規律 §9.1）。
            assert_eq!(stated.layout, expected_name);
            assert_eq!(stated.channel_count(), expected_channels);
            assert_eq!(stated.mapping_revision, 1);
        }

        // Bypassed / Inactive の最小 JSON は配置を名乗らない（測っていないため）。
        signal_state.store(SignalState::Bypassed as u8, Ordering::Release);
        write_json(
            &file,
            "pre-instance",
            "Mix",
            "daw",
            "owner",
            &result,
            &signal_state,
            crate::channel_layout::ChannelLayout::stereo(),
        )
        .unwrap();
        let parsed: serde_json::Value =
            serde_json::from_str(&std::fs::read_to_string(&file).unwrap()).unwrap();
        assert!(parsed.get("layout").is_none(), "測っていないなら名乗らない");

        let _ = std::fs::remove_dir_all(&dir);
    }
