mod ci_usage_guard;
mod diagnose_watch;
mod gen_signals;
mod install;
mod macos_codesign;
mod notarize;
mod release_gate;
mod release_package;
mod rt_safety;
mod shell_parity;
mod stamp_version;
mod windows_preflight;
mod windows_readiness;

fn main() -> nih_plug_xtask::Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(|s| s.as_str()) {
        Some("gen-signals") => {
            args.remove(0);
            gen_signals::run(args)
        }
        Some("diagnose-watch") => {
            args.remove(0);
            diagnose_watch::run(args)
        }
        Some("ci-usage-guard") => {
            args.remove(0);
            ci_usage_guard::run(args)
        }
        Some("install") => {
            args.remove(0);
            install::run(args)
        }
        Some("notarize") => {
            args.remove(0);
            notarize::run(args)
        }
        Some("release-package") => {
            args.remove(0);
            release_package::run(args)
        }
        // B-109: nih_plug_xtask が Info.plist 版を 1.0.0 固定で出力するため、bundle 後に egui VST3
        // の版を crate 版へ stamp する（版統一 / G-115-345）。プラグイン挙動・計測は無変更。
        Some("stamp-egui-version") => {
            args.remove(0);
            stamp_version::run(args)
        }
        Some("windows-preflight") => {
            args.remove(0);
            windows_preflight::run(args)
        }
        Some("windows-readiness") => {
            args.remove(0);
            windows_readiness::run(args)
        }
        _ => nih_plug_xtask::main_with_args("cargo xtask", args),
    }
}
