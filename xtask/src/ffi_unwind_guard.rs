//! Every Rust function the shipping product exports through the C ABI runs inside `catch_unwind`.
//!
//! A panic that crosses `extern "C"` aborts the DAW together with the user's session. The rule is
//! checked over the sources rather than over a hand-kept list, so a new entry cannot be added
//! without a guard. It is structural: the whole body of an `extern "<abi>" fn` is one
//! `catch_unwind(..)` of a closure (or of a function path), either discarded with `let _ = ..;` or
//! followed by `.unwrap_or(<constant>)`. Nothing else in the body runs outside the guard.
//!
//! The scanned crates are the ones linked into the shipping product (see `ffi_unwind_scope.rs`).
//! There the check also fails on an export the scanner cannot see as a guarded entry
//! (`#[no_mangle]`/`export_name` elsewhere), an export macro, an item-level macro invocation that
//! is neither a local `macro_rules!` nor a std allowlisted one, or a shadowed `catch_unwind`.

#[path = "ffi_unwind_scope.rs"]
mod scope;
#[cfg(test)]
#[path = "ffi_unwind_guard_tests.rs"]
mod tests;

#[derive(Clone, Copy, PartialEq, Debug)]
enum Kind {
    Ident,
    Punct,
    Literal,
}

#[derive(Debug)]
struct Token<'a> {
    text: &'a str,
    kind: Kind,
    start: usize,
}

impl Token<'_> {
    fn is(&self, text: &str) -> bool {
        self.kind != Kind::Literal && self.text == text
    }
}

#[derive(Debug, PartialEq)]
struct Entry {
    name: String,
    line: usize,
    guarded: bool,
}

#[derive(Debug, Default)]
struct Scan {
    entries: Vec<Entry>,
    /// Exports the scanner cannot see as entries, export macros, and `catch_unwind` shadowing.
    problems: Vec<String>,
}

fn is_ident_byte(source: &[u8], index: usize) -> bool {
    source
        .get(index)
        .is_some_and(|&b| b.is_ascii_alphanumeric() || b == b'_' || b == b'$')
}

/// Code tokens only: comments and whitespace separate tokens and never reach the rules.
/// Comment markers are byte patterns: the native storage guard refuses them between string literals.
fn tokens(source: &str) -> Vec<Token<'_>> {
    let bytes = source.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        let rest = &bytes[i..];
        let (end, kind) = if rest[0].is_ascii_whitespace() {
            i += 1;
            continue;
        } else if matches!(rest, [b'/', b'/', ..]) {
            i = rest
                .iter()
                .position(|&b| b == b'\n')
                .map_or(bytes.len(), |n| i + n);
            continue;
        } else if matches!(rest, [b'/', b'*', ..]) {
            i = block_comment_end(bytes, i);
            continue;
        } else if let Some(hashes) = raw_string_start(bytes, i) {
            (raw_string_end(bytes, i, hashes), Kind::Literal)
        } else if matches!(rest, [b'b', b'"' | b'\'', ..])
            && !is_ident_byte(bytes, i.wrapping_sub(1))
        {
            (quoted_end(bytes, i + 1, rest[1]), Kind::Literal)
        } else if rest[0] == b'"' {
            (quoted_end(bytes, i, b'"'), Kind::Literal)
        } else if rest[0] == b'\'' && is_char_literal(rest) {
            (quoted_end(bytes, i, b'\''), Kind::Literal)
        } else if rest[0] == b'\'' || is_ident_byte(bytes, i) {
            let tail = (i + 1..bytes.len()).find(|&n| !is_ident_byte(bytes, n));
            (tail.unwrap_or(bytes.len()), Kind::Ident)
        } else if matches!(rest, [b':', b':', ..] | [b'|', b'|', ..] | [b'-', b'>', ..]) {
            (i + 2, Kind::Punct)
        } else {
            let width = source[i..].chars().next().map_or(1, char::len_utf8);
            (i + width, Kind::Punct)
        };
        out.push(Token {
            text: &source[i..end],
            kind,
            start: i,
        });
        i = end;
    }
    out
}

fn block_comment_end(source: &[u8], start: usize) -> usize {
    let (mut depth, mut i) = (0usize, start);
    while i + 1 < source.len() {
        match &source[i..i + 2] {
            [b'/', b'*'] => (depth, i) = (depth + 1, i + 2),
            [b'*', b'/'] => {
                (depth, i) = (depth - 1, i + 2);
                if depth == 0 {
                    return i;
                }
            }
            _ => i += 1,
        }
    }
    source.len()
}

/// `r"`, `r#"`, `br#"`: the number of `#` when a raw string starts here.
fn raw_string_start(source: &[u8], i: usize) -> Option<usize> {
    if is_ident_byte(source, i.wrapping_sub(1)) {
        return None;
    }
    let after_prefix = match source.get(i..)? {
        [b'b', b'r', ..] => i + 2,
        [b'r', ..] => i + 1,
        _ => return None,
    };
    let hashes = source[after_prefix..]
        .iter()
        .take_while(|&&b| b == b'#')
        .count();
    (source.get(after_prefix + hashes) == Some(&b'"')).then_some(hashes)
}

fn raw_string_end(source: &[u8], start: usize, hashes: usize) -> usize {
    let open = start + source[start..].iter().position(|&b| b == b'"').unwrap_or(0) + 1;
    let mut close = vec![b'"'];
    close.extend(std::iter::repeat_n(b'#', hashes));
    source[open..]
        .windows(close.len())
        .position(|window| window == close.as_slice())
        .map_or(source.len(), |n| open + n + close.len())
}

fn quoted_end(source: &[u8], open: usize, quote: u8) -> usize {
    let mut i = open + 1;
    while i < source.len() {
        match source[i] {
            b'\\' => i += 2,
            b if b == quote => return i + 1,
            _ => i += 1,
        }
    }
    source.len()
}

/// `'x'`, `'\n'`, `'\u{..}'` and multi-byte chars are literals; `'a` lifetimes and labels are not.
fn is_char_literal(rest: &[u8]) -> bool {
    if rest.get(1) == Some(&b'\\') {
        return true;
    }
    let text = std::str::from_utf8(&rest[1..rest.len().min(6)])
        .unwrap_or_else(|e| std::str::from_utf8(&rest[1..1 + e.valid_up_to()]).unwrap_or_default());
    let mut chars = text.chars();
    chars.next().is_some() && chars.next() == Some('\'')
}

/// Index of the bracket that closes the one at `open`, counting all three bracket kinds.
fn closing(tokens: &[Token], open: usize) -> Option<usize> {
    let mut depth = 0i32;
    for (index, token) in tokens.iter().enumerate().skip(open) {
        match token.text {
            "(" | "[" | "{" if token.kind == Kind::Punct => depth += 1,
            ")" | "]" | "}" if token.kind == Kind::Punct => {
                depth -= 1;
                if depth == 0 {
                    return Some(index);
                }
            }
            _ => {}
        }
    }
    None
}

fn starts_with(tokens: &[Token], texts: &[&str]) -> bool {
    tokens.len() >= texts.len() && tokens.iter().zip(texts).all(|(t, text)| t.is(text))
}

/// Skip an optional `std::panic::` or `panic::` prefix before `name`.
fn after_path<'t, 'a>(tokens: &'t [Token<'a>], name: &str) -> Option<&'t [Token<'a>]> {
    for prefix in [&["std", "::", "panic", "::"][..], &["panic", "::"], &[]] {
        if starts_with(tokens, prefix) && tokens.get(prefix.len()).is_some_and(|t| t.is(name)) {
            return Some(&tokens[prefix.len() + 1..]);
        }
    }
    None
}

/// The whole body is `[let _ =] catch_unwind(<closure or path>)` plus `;` or a constant fallback.
fn body_is_guarded(body: &[Token]) -> bool {
    let (rest, discarded) = if starts_with(body, &["let", "_", "="]) {
        (&body[3..], true)
    } else {
        (body, false)
    };
    let Some(call) = after_path(rest, "catch_unwind") else {
        return false;
    };
    let Some(close) = call.first().filter(|t| t.is("(")).and(closing(call, 0)) else {
        return false;
    };
    let (argument, tail) = (&call[1..close], &call[close + 1..]);
    if !guarded_argument(argument) {
        return false;
    }
    if discarded {
        return tail.len() == 1 && tail[0].is(";");
    }
    // `.unwrap_or_default()` would run a `Default` outside the guard: only constants follow it.
    tail.is_empty()
        || (starts_with(tail, &[".", "unwrap_or", "("])
            && closing(tail, 2) == Some(tail.len() - 1)
            && constant(&tail[3..tail.len() - 1]))
}

fn guarded_argument(argument: &[Token]) -> bool {
    let closure = |t: &[Token]| {
        let t = if starts_with(t, &["move"]) {
            &t[1..]
        } else {
            t
        };
        t.first()
            .is_some_and(|first| first.is("||") || first.is("|"))
    };
    let path = |t: &[Token]| !t.is_empty() && t.iter().all(|t| t.kind == Kind::Ident || t.is("::"));
    if closure(argument) {
        return true;
    }
    let Some(wrapped) = after_path(argument, "AssertUnwindSafe") else {
        return false;
    };
    wrapped.first().is_some_and(|t| t.is("("))
        && closing(wrapped, 0) == Some(wrapped.len() - 1)
        && (closure(&wrapped[1..wrapped.len() - 1]) || path(&wrapped[1..wrapped.len() - 1]))
}

/// A fallback evaluated after the guard returns: names, literals and casts only, or a null pointer.
fn constant(tokens: &[Token]) -> bool {
    !tokens.is_empty()
        && tokens
            .iter()
            .enumerate()
            .all(|(index, token)| match token.kind {
                Kind::Literal | Kind::Ident => true,
                Kind::Punct => match token.text {
                    "::" | "-" => true,
                    "(" => index > 0 && matches!(tokens[index - 1].text, "null" | "null_mut"),
                    ")" => index > 0 && tokens[index - 1].is("("),
                    _ => false,
                },
            })
}

#[cfg(test)]
fn scan(source: &str) -> Scan {
    scan_with(source, &[])
}

/// The `macro_rules!` names one source defines; a crate's macros may be invoked in its other files.
fn defined_macros(source: &str) -> Vec<String> {
    let tokens = tokens(source);
    tokens
        .windows(3)
        .filter(|w| w[0].is("macro_rules") && w[1].is("!"))
        .map(|w| w[2].text.to_string())
        .collect()
}

/// Scans one source; `crate_macros` are the `macro_rules!` names defined anywhere in its crate.
fn scan_with(source: &str, crate_macros: &[String]) -> Scan {
    let tokens = tokens(source);
    let line = |at: usize| source[..at].matches('\n').count() + 1;
    let mut scan = Scan::default();
    let mut entry_fns = Vec::new();
    for (at, token) in tokens.iter().enumerate() {
        if !token.is("extern") {
            continue;
        }
        let mut next = at + 1;
        if tokens.get(next).is_some_and(|t| t.kind == Kind::Literal) {
            next += 1;
        }
        let (Some(keyword), Some(name)) = (tokens.get(next), tokens.get(next + 1)) else {
            continue;
        };
        if !keyword.is("fn") || name.kind != Kind::Ident {
            continue; // `extern crate`, `extern "C" {}`, or a function pointer type
        }
        let Some((open, close)) = body_after(&tokens, next + 2) else {
            continue; // a declaration without a body
        };
        entry_fns.push(next);
        scan.entries.push(Entry {
            name: name.text.to_string(),
            line: line(token.start),
            guarded: body_is_guarded(&tokens[open..close]),
        });
    }
    let mut macros: Vec<&str> = tokens
        .windows(3)
        .filter(|w| w[0].is("macro_rules") && w[1].is("!"))
        .map(|w| w[2].text)
        .collect();
    macros.extend(crate_macros.iter().map(String::as_str));
    let bodies = code_bodies(&tokens);
    for (at, token) in tokens.iter().enumerate() {
        let invoked = token.kind == Kind::Ident
            && !token.is("macro_rules")
            && tokens.get(at + 1).is_some_and(|t| t.is("!"))
            && tokens
                .get(at + 2)
                .is_some_and(|t| t.is("(") || t.is("[") || t.is("{"));
        let item_level = !bodies.iter().any(|body| body.contains(&at));
        if invoked && item_level && !allowed_item_macro(&tokens, at, &macros) {
            scan.problems.push(format!(
                "line {}: item-level macro {}",
                line(token.start),
                token.text
            ));
        }
        let next = tokens.get(at + 1);
        if token.kind == Kind::Ident
            && token.text.contains("export")
            && token.text != "export_name"
            && (next.is_some_and(|t| t.is("!")) || attribute_name(&tokens, at))
            && !macros.contains(&token.text)
        {
            scan.problems.push(format!(
                "line {}: export macro {}",
                line(token.start),
                token.text
            ));
        }
        if (token.is("no_mangle") || token.is("export_name")) && !attached(&tokens, at, &entry_fns)
        {
            scan.problems.push(format!(
                "line {}: export without a seen entry",
                line(token.start)
            ));
        }
        let named = if token.is("macro_rules") {
            tokens.get(at + 2)
        } else {
            next
        };
        let shadow = [
            "fn",
            "as",
            "struct",
            "enum",
            "type",
            "union",
            "trait",
            "macro_rules",
        ]
        .iter()
        .any(|d| token.is(d));
        if shadow && named.is_some_and(|t| t.is("catch_unwind") || t.is("AssertUnwindSafe")) {
            scan.problems
                .push(format!("line {}: shadows the std guard", line(token.start)));
        }
    }
    scan
}

/// Token ranges where a macro cannot add an item to the module: function and `macro_rules!` bodies
/// (a template is scanned in place).
fn code_bodies(tokens: &[Token]) -> Vec<std::ops::Range<usize>> {
    let mut bodies = Vec::new();
    for (at, token) in tokens.iter().enumerate() {
        let named = tokens.get(at + 1).is_some_and(|t| t.kind == Kind::Ident);
        let body = if token.is("fn") && named {
            body_after(tokens, at + 2)
        } else if token.is("macro_rules") && tokens.get(at + 3).is_some_and(|t| t.is("{")) {
            closing(tokens, at + 3).map(|close| (at + 4, close))
        } else {
            None
        };
        bodies.extend(body.map(|(open, close)| open..close));
    }
    bodies
}

/// A local `macro_rules!` (its template is scanned), or `thread_local!`, `assert!` and an
/// `include!` of a sibling file, optionally as `std::`/`core::` paths. Anything else could expand
/// to an export the scanner cannot see, whatever name it is imported under.
fn allowed_item_macro(tokens: &[Token], at: usize, macros: &[&str]) -> bool {
    let qualified = at >= 2 && tokens[at - 1].is("::");
    let prefix = qualified.then(|| tokens[at - 2].text);
    let name = tokens[at].text;
    match (prefix, name) {
        (None | Some("crate" | "$crate"), _) if macros.contains(&name) => true,
        (None | Some("std" | "core"), "thread_local" | "assert") => true,
        (None | Some("std" | "core"), "include") => {
            let argument = tokens.get(at + 3);
            argument.is_some_and(|t| {
                t.kind == Kind::Literal && !t.text.contains('/') && !t.text.contains('\\')
            }) && tokens.get(at + 4).is_some_and(|t| t.is(")"))
        }
        _ => false,
    }
}

/// The braces of the body after a signature that starts at `from`, or `None` when `;` ends it.
fn body_after(tokens: &[Token], from: usize) -> Option<(usize, usize)> {
    let mut nesting = 0;
    for index in from..tokens.len() {
        match tokens[index].text {
            "(" | "[" | "<" => nesting += 1,
            ")" | "]" | ">" => nesting -= 1,
            ";" if nesting <= 0 => return None,
            "{" if nesting <= 0 => return closing(tokens, index).map(|close| (index + 1, close)),
            _ => {}
        }
    }
    None
}

/// `#[name`, `#[path::name` or `#![name`: an attribute, possibly a procedural macro that exports.
fn attribute_name(tokens: &[Token], mut at: usize) -> bool {
    while at >= 2 && tokens[at - 1].is("::") && tokens[at - 2].kind == Kind::Ident {
        at -= 2;
    }
    at >= 2 && tokens[at - 1].is("[") && (tokens[at - 2].is("#") || tokens[at - 2].is("!"))
}

/// The item an export attribute marks is the next `fn`, and it must be a discovered entry.
fn attached(tokens: &[Token], attribute: usize, entry_fns: &[usize]) -> bool {
    let mut index = attribute;
    while index < tokens.len() && !tokens[index].is("]") {
        index += 1;
    }
    for (offset, token) in tokens.iter().enumerate().skip(index + 1) {
        if token.is("fn") {
            return entry_fns.contains(&offset);
        }
        if token.is(";") || token.is("{") || token.is("}") {
            return false;
        }
    }
    false
}
