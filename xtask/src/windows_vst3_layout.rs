use anyhow::{bail, Result};

const REPO_ROOT: &str = ".";
const USER_VST3_ROOT: &str = "%LOCALAPPDATA%\\Programs\\Common\\VST3";
const GLOBAL_VST3_ROOT: &str = "%ProgramFiles%\\Common Files\\VST3";

pub fn run(args: Vec<String>) -> Result<()> {
    match args.as_slice() {
        [] => {}
        [arg] if arg == "-h" || arg == "--help" => {
            print_usage();
            return Ok(());
        }
        [arg] => bail!("unknown argument: {arg}"),
        _ => bail!("unknown arguments: {}", args.join(" ")),
    }

    eprintln!(
        "[windows-vst3-layout] Windows VST3 layout. Source is JUCE VST3 build output; \
         install roots follow Steinberg VST3 predefined locations."
    );
    for bundle in windows_vst3_bundles(REPO_ROOT, USER_VST3_ROOT, GLOBAL_VST3_ROOT) {
        eprintln!("{}:", bundle.role);
        eprintln!("  source:      {}", bundle.source);
        eprintln!("  user-dev:    {}", bundle.user_dest);
        eprintln!("  global-ship: {}", bundle.global_dest);
    }
    Ok(())
}

fn print_usage() {
    eprintln!(
        "Usage: cargo run -p xtask -- windows-vst3-layout\n\n\
         Prints the expected Windows VST3 artifact layout for PRE/POST. This is a\n\
         read-only planning command: it does not install or copy bundles."
    );
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct WindowsVst3Bundle {
    role: &'static str,
    source: String,
    user_dest: String,
    global_dest: String,
}

fn windows_vst3_bundles(
    repo_root: &str,
    user_vst3_root: &str,
    global_vst3_root: &str,
) -> Vec<WindowsVst3Bundle> {
    ["PRE", "POST"]
        .into_iter()
        .map(|role| {
            let file = plugin_file(role);
            WindowsVst3Bundle {
                role,
                source: repo_join(
                    repo_root,
                    &[
                        "juce_shell",
                        "build-windows",
                        &format!("KirinHypha{role}_artefacts"),
                        "Release",
                        "VST3",
                        &file,
                    ],
                ),
                user_dest: win_join(user_vst3_root, &[&file]),
                global_dest: win_join(global_vst3_root, &[&file]),
            }
        })
        .collect()
}

pub(crate) fn windows_vst3_artifact_paths() -> Vec<String> {
    ["PRE", "POST"]
        .into_iter()
        .map(|role| {
            repo_join(
                "",
                &[
                    "juce_shell",
                    "build-windows",
                    &format!("KirinHypha{role}_artefacts"),
                    "Release",
                    "VST3",
                    &plugin_file(role),
                ],
            )
        })
        .collect()
}

fn plugin_file(role: &str) -> String {
    format!("Kirin Hypha {role}.vst3")
}

fn repo_join(root: &str, segments: &[&str]) -> String {
    let mut out = root.trim_end_matches('/').to_string();
    for segment in segments {
        if !out.is_empty() {
            out.push('/');
        }
        out.push_str(segment.trim_matches('/'));
    }
    out
}

fn win_join(root: &str, segments: &[&str]) -> String {
    let mut out = root.trim_end_matches(['\\', '/']).to_string();
    for segment in segments {
        out.push('\\');
        out.push_str(segment.trim_matches(['\\', '/']));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn windows_layout_includes_pre_and_post_vst3_artifacts() {
        let bundles = windows_vst3_bundles(".", USER_VST3_ROOT, GLOBAL_VST3_ROOT);
        assert_eq!(bundles.len(), 2);
        assert_eq!(bundles[0].role, "PRE");
        assert_eq!(bundles[1].role, "POST");
        assert!(bundles[0]
            .source
            .ends_with("KirinHyphaPRE_artefacts/Release/VST3/Kirin Hypha PRE.vst3"));
        assert!(bundles[1]
            .source
            .ends_with("KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3"));
    }

    #[test]
    fn windows_layout_exports_ci_artifact_paths() {
        assert_eq!(
            windows_vst3_artifact_paths(),
            vec![
                "juce_shell/build-windows/KirinHyphaPRE_artefacts/Release/VST3/Kirin Hypha PRE.vst3",
                "juce_shell/build-windows/KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3",
            ]
        );
    }

    #[test]
    fn windows_layout_uses_steinberg_user_dev_folder() {
        let local_appdata = r"C:\Users\Daisuke\AppData\Local";
        let user_root = win_join(local_appdata, &["Programs", "Common", "VST3"]);
        let bundles = windows_vst3_bundles(".", &user_root, GLOBAL_VST3_ROOT);

        assert_eq!(
            bundles[0].user_dest,
            r"C:\Users\Daisuke\AppData\Local\Programs\Common\VST3\Kirin Hypha PRE.vst3"
        );
        assert_eq!(
            bundles[1].user_dest,
            r"C:\Users\Daisuke\AppData\Local\Programs\Common\VST3\Kirin Hypha POST.vst3"
        );
    }

    #[test]
    fn windows_layout_uses_global_common_files_vst3() {
        let program_files = r"C:\Program Files";
        let global_root = win_join(program_files, &["Common Files", "VST3"]);
        let bundles = windows_vst3_bundles(".", USER_VST3_ROOT, &global_root);

        assert_eq!(
            bundles[0].global_dest,
            r"C:\Program Files\Common Files\VST3\Kirin Hypha PRE.vst3"
        );
        assert_eq!(
            bundles[1].global_dest,
            r"C:\Program Files\Common Files\VST3\Kirin Hypha POST.vst3"
        );
    }

    #[test]
    fn windows_layout_excludes_macos_and_au_paths() {
        let rendered = windows_vst3_bundles(".", USER_VST3_ROOT, GLOBAL_VST3_ROOT)
            .into_iter()
            .flat_map(|b| [b.source, b.user_dest, b.global_dest])
            .collect::<Vec<_>>()
            .join("\n");

        for forbidden in [
            ".component",
            "/Library/Audio/Plug-Ins",
            "Components",
            "build-universal",
            "codesign",
            "notarize",
            "LS_UPLOAD",
        ] {
            assert!(!rendered.contains(forbidden), "found {forbidden}");
        }
        assert_eq!(rendered.matches(".vst3").count(), 6);
    }
}
