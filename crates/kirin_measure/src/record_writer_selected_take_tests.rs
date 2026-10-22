use super::*;

#[test]
fn the_record_names_its_selected_take_epoch_and_preserves_legacy_json() {
    let loaded = close_tracked_keep(None, "selected-take-epoch");
    let epoch = loaded
        .trace_selected_capture_epoch
        .expect("the writer names its selected take");
    assert!(epoch > 0);
    let mut json = serde_json::to_value(&loaded).unwrap();
    let roundtrip: PluginDataFile = serde_json::from_value(json.clone()).unwrap();
    assert_eq!(roundtrip.trace_selected_capture_epoch, Some(epoch));
    assert_eq!(roundtrip.schema_version, "1.3");
    json.as_object_mut()
        .unwrap()
        .remove("trace_selected_capture_epoch");
    let legacy: PluginDataFile = serde_json::from_value(json).unwrap();
    assert_eq!(legacy.trace_selected_capture_epoch, None);
}
