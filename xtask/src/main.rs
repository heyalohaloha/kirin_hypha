mod gen_signals;

fn main() -> nih_plug_xtask::Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    if args.first().map(|s| s.as_str()) == Some("gen-signals") {
        args.remove(0);
        return gen_signals::run(args);
    }
    nih_plug_xtask::main_with_args("cargo xtask", args)
}
