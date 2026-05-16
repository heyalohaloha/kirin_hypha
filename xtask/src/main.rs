mod gen_signals;
mod notarize;

fn main() -> nih_plug_xtask::Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(|s| s.as_str()) {
        Some("gen-signals") => {
            args.remove(0);
            gen_signals::run(args)
        }
        Some("notarize") => {
            args.remove(0);
            notarize::run(args)
        }
        _ => nih_plug_xtask::main_with_args("cargo xtask", args),
    }
}
