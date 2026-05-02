mod gen_signals;
mod install;

fn main() -> nih_plug_xtask::Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    if args.first().map(|s| s.as_str()) == Some("gen-signals") {
        args.remove(0);
        return gen_signals::run(args);
    }
    // B-022 段階 6 (G-115-37): VST3 install サブコマンド。bundle 完了後に
    // user-level 古バイナリを除去 + system-level に最新を sudo 配置する。
    // user-level に古いバイナリが滞留して Studio One が読込み続ける事故を
    // 構造で禁止するため、cargo run --package xtask -- install --release を
    // 唯一の正規経路として採用 (CLAUDE.md「ビルド・テスト」参照)。
    if args.first().map(|s| s.as_str()) == Some("install") {
        args.remove(0);
        return install::run(args);
    }
    nih_plug_xtask::main_with_args("cargo xtask", args)
}
