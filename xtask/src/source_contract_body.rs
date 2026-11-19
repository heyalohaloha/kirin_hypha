fn identifier(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_'
}
fn number_separator(bytes: &[u8], i: usize) -> bool {
    let start = bytes[..i]
        .iter()
        .rposition(|b| !identifier(*b))
        .map_or(0, |n| n + 1);
    start < i
        && bytes[start].is_ascii_digit()
        && bytes.get(i + 1).is_some_and(u8::is_ascii_alphanumeric)
}

// Preserve byte offsets while removing comments and C++ string/character literals.
fn code_only(source: &str) -> Vec<u8> {
    let bytes = source.as_bytes();
    let mut code = bytes.to_vec();
    let mut i = 0;
    while i < bytes.len() {
        let start = i;
        if bytes[i..].starts_with(b"//") {
            i += 2;
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
        // Compare bytes because the adjacent guard reads comment tokens inside literals.
        } else if bytes[i] == b'/' && bytes.get(i + 1) == Some(&b'*') {
            i += 2;
            while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') {
                i += 1;
            }
            assert!(i + 1 < bytes.len(), "unterminated comment");
            i += 2;
        } else if bytes[i..].starts_with(b"R\"") {
            let delimiter = i + 2;
            let open = delimiter
                + bytes[delimiter..]
                    .iter()
                    .position(|b| *b == b'(')
                    .expect("raw string opens");
            assert!(open - delimiter <= 16, "invalid raw string delimiter");
            let mut end = vec![b')'];
            end.extend_from_slice(&bytes[delimiter..open]);
            end.push(b'"');
            i = open
                + 1
                + bytes[open + 1..]
                    .windows(end.len())
                    .position(|s| s == end)
                    .expect("raw string closes")
                + end.len();
        } else if bytes[i] == b'"' || (bytes[i] == b'\'' && !number_separator(bytes, i)) {
            let quote = bytes[i];
            i += 1;
            loop {
                assert!(i < bytes.len(), "unterminated literal");
                if bytes[i] == b'\\' {
                    i += 2;
                } else if bytes[i] == quote {
                    i += 1;
                    break;
                } else {
                    i += 1;
                }
            }
        } else {
            i += 1;
            continue;
        }
        for byte in &mut code[start..i] {
            if *byte != b'\n' {
                *byte = b' ';
            }
        }
    }
    code
}

fn close_group(code: &[u8], open: usize, left: u8, right: u8) -> usize {
    let mut depth = 0;
    for (offset, ch) in code[open..].iter().enumerate() {
        if *ch == left {
            depth += 1;
        } else if *ch == right {
            depth -= 1;
            if depth == 0 {
                return open + offset;
            }
        }
    }
    panic!("unbalanced group");
}

// This helper selects boundaries; it returns the original body for existing literal contracts.
// A declaration cannot borrow the following function's body. Ambiguous definitions fail closed.
pub(super) fn function_body<'a>(source: &'a str, signature: &str) -> &'a str {
    let code = code_only(source);
    let needle = signature.as_bytes();
    assert!(!needle.is_empty());
    let mut bodies = Vec::new();
    for start in 0..=code.len().saturating_sub(needle.len()) {
        if code.get(start..start + needle.len()) != Some(needle) {
            continue;
        }
        if (start > 0 && identifier(needle[0]) && identifier(code[start - 1]))
            || (identifier(*needle.last().unwrap())
                && code
                    .get(start + needle.len())
                    .is_some_and(|b| identifier(*b)))
        {
            continue;
        }
        let parameters = start
            + code[start..]
                .iter()
                .position(|b| *b == b'(')
                .expect("parameters open");
        let mut i = close_group(&code, parameters, b'(', b')') + 1;
        while i < code.len() {
            match code[i] {
                b';' | b'=' => break, // forward, deleted or defaulted declaration
                b'(' => i = close_group(&code, i, b'(', b')') + 1, // noexcept(expression)
                b'{' => {
                    bodies.push((i, close_group(&code, i, b'{', b'}') + 1));
                    break;
                }
                _ => i += 1,
            }
        }
    }
    assert_eq!(bodies.len(), 1, "expected one definition for {signature}");
    &source[bodies[0].0..bodies[0].1]
}

#[test]
fn braced_defaults_are_not_function_bodies() {
    let source = "Decision process (Value a = Value(), Late late = {}) noexcept { if (a) { copy(); } fence(); }";
    assert_eq!(
        function_body(source, "Decision process ("),
        "{ if (a) { copy(); } fence(); }"
    );
}

#[test]
fn declarations_cannot_borrow_another_body() {
    let source =
        "void target(int); void other() { harmless(); } void target(int x) { dangerous(); }";
    assert_eq!(function_body(source, "void target("), "{ dangerous(); }");
}

#[test]
fn comments_and_literals_do_not_change_the_definition() {
    let source = r###"// void target() { fake(); }
const char* decoy = "void target() { fake(); }";
/* void target() { fake(); } */
void target(const char* x = ") {", char c = '\'') noexcept(noexcept(other())) {
  const char* raw = R"tag( } /* ) \" )tag";
  const char* escaped = "\\\" }"; /* } */
  actual(); // }
}"###;
    let body = function_body(source, "void target(");
    assert!(body.contains("actual();") && body.ends_with("\n}"));
}

#[test]
#[should_panic(expected = "expected one definition")]
fn declaration_only_is_rejected() {
    function_body("void target(); void other() {}", "void target(");
}

#[test]
#[should_panic(expected = "expected one definition")]
fn duplicate_definitions_are_rejected() {
    function_body("void target() {} void target(int) {}", "void target(");
}

#[test]
fn partial_identifier_matches_and_hex_separators_are_not_definitions() {
    let source = "void targetExtra() {} void target(int x = 0xCAFE'BEEF) { actual(); }";
    assert_eq!(function_body(source, "void target"), "{ actual(); }");
}
