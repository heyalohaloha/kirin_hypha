// Find the body after the complete parameter list, not a braced default argument.
pub(super) fn function_body<'a>(source: &'a str, signature: &str) -> &'a str {
    let start = source.find(signature).expect("the function exists");
    let parameters = start + source[start..].find('(').expect("parameters open");
    let mut depth = 0usize;
    let close = source[parameters..]
        .char_indices()
        .find_map(|(offset, ch)| {
            if ch == '(' {
                depth += 1;
            }
            if ch == ')' {
                depth -= 1;
                if depth == 0 {
                    return Some(parameters + offset);
                }
            }
            None
        })
        .expect("parameters close");
    let open = close + source[close..].find('{').expect("the body opens");
    for (offset, ch) in source[open..].char_indices() {
        match ch {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    return &source[open..open + offset + 1];
                }
            }
            _ => {}
        }
    }
    panic!("unbalanced body for {signature}");
}

#[test]
fn braced_defaults_are_not_function_bodies() {
    let source = "Decision process (Value a = Value(), Late late = {}) noexcept { if (a) { copy(); } fence(); }";
    let body = function_body(source, "Decision process (");
    assert_eq!(body, "{ if (a) { copy(); } fence(); }");
    assert!(!body.contains("late ="));
}
