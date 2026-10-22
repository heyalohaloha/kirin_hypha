//! Read a unique shelf entry; directory iteration order must never choose the Record under test.
use std::path::{Path, PathBuf};

pub fn find_json_under(root: &Path, parent_name: &str, file_name: &str) -> Option<PathBuf> {
    fn collect(root: &Path, parent_name: &str, file_name: &str, found: &mut Vec<PathBuf>) {
        let entries = match std::fs::read_dir(root) {
            Ok(entries) => entries,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return,
            Err(error) => panic!("cannot read {}: {error}", root.display()),
        };
        for entry in entries {
            let entry = entry.unwrap();
            let path = entry.path();
            let kind = entry.file_type().unwrap();
            if kind.is_dir() {
                collect(&path, parent_name, file_name, found);
            } else if kind.is_file() {
                let name_ok = if file_name == "*.json" {
                    path.extension().is_some_and(|ext| ext == "json")
                } else {
                    path.file_name().is_some_and(|name| name == file_name)
                };
                let parent_ok = parent_name.is_empty()
                    || path
                        .parent()
                        .and_then(Path::file_name)
                        .is_some_and(|name| name == parent_name);
                if name_ok && parent_ok {
                    found.push(path);
                }
            }
        }
    }
    let mut found = Vec::new();
    collect(root, parent_name, file_name, &mut found);
    assert!(
        found.len() <= 1,
        "expected one shelf JSON under {root:?}/{parent_name}, found {found:?}"
    );
    found.pop()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_multiple_records_and_ignores_hidden_members() {
        let fixture = tempfile::tempdir().unwrap();
        let root = fixture.path();
        let role = root.join("pre");
        std::fs::create_dir_all(role.join(".pair_committed")).unwrap();
        std::fs::write(role.join(".pair_committed/member.json"), b"{}").unwrap();
        assert!(find_json_under(root, "pre", "*.json").is_none());
        std::fs::write(role.join("first.json"), b"{}").unwrap();
        assert_eq!(
            find_json_under(root, "pre", "*.json"),
            Some(role.join("first.json"))
        );
        std::fs::write(role.join("second.json"), b"{}").unwrap();
        let rejected = std::panic::catch_unwind(|| find_json_under(root, "pre", "*.json"));
        assert!(rejected.is_err());
    }
}
