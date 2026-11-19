//! Fallbacks run outside catch_unwind. Calls require verified std/core pointer constructors,
//! not a spelling such as `null_mut` which a local function can also use.

use super::{closing, starts_with, Kind, Token};

pub(super) fn constant(value: &[Token], source: &[Token]) -> bool {
    if value.iter().any(|t| t.is("(")) {
        return null_pointer(value, source);
    }
    !value.is_empty()
        && value
            .iter()
            .all(|t| matches!(t.kind, Kind::Literal | Kind::Ident) || t.is("::") || t.is("-"))
}

pub(super) fn standard_root_unshadowed(root: &str, source: &[Token]) -> bool {
    // A declaration, alias, argument or binding named std/core cannot certify the standard path.
    // Conservatively refuse ambiguous forms rather than infer what an arbitrary import means.
    source.iter().enumerate().all(|(i, t)| {
        !t.is(root)
            || (source.get(i + 1).is_some_and(|t| t.is("::"))
                && !i.checked_sub(1).is_some_and(|n| {
                    matches!(source[n].text, "as" | "mod" | "fn" | "let" | "crate")
                }))
    })
}

/// Which standard namespaces are actually used as a fallback, not unrelated variables
/// called `core` in the DSP implementation. Imports certify aliases only after local checks.
pub(super) fn uses_pointer_root(root: &str, source: &[Token]) -> bool {
    imports(source).iter().any(|i| {
        i.path.first() == Some(&root)
            && matches!(i.path.as_slice(), [_, "ptr", "null" | "null_mut"])
    }) || source.iter().enumerate().any(|(i, t)| {
        if !t.is("unwrap_or") {
            return false;
        }
        let mut rest = &source[i + 1..];
        if !starts_with(rest, &["("]) {
            return false;
        }
        rest = &rest[1..];
        if starts_with(rest, &["::"]) {
            rest = &rest[1..];
        }
        starts_with(rest, &[root, "::", "ptr", "::"])
    })
}

fn standard_pointer(path: &[&str], source: &[Token]) -> bool {
    matches!(path, ["std" | "core", "ptr", "null" | "null_mut"])
        && standard_root_unshadowed(path[0], source)
}

#[derive(Debug)]
struct Import<'a> {
    name: &'a str,
    path: Vec<&'a str>,
    scope: Vec<usize>,
    span: std::ops::Range<usize>,
}

/// A deliberately small use-tree reader. Unsupported syntax creates no trusted binding.
fn use_tree<'a>(tree: &[Token<'a>], prefix: &[&'a str], out: &mut Vec<(&'a str, Vec<&'a str>)>) {
    let mut path = prefix.to_vec();
    let mut i = usize::from(tree.first().is_some_and(|t| t.is("::")));
    while i < tree.len() {
        if tree[i].is("{") {
            if closing(tree, i) != Some(tree.len() - 1) {
                return;
            }
            let mut start = i + 1;
            let mut depth = 0;
            for n in i + 1..tree.len() - 1 {
                if tree[n].is("{") {
                    depth += 1;
                }
                if tree[n].is("}") {
                    depth -= 1;
                }
                if tree[n].is(",") && depth == 0 {
                    use_tree(&tree[start..n], &path, out);
                    start = n + 1;
                }
            }
            use_tree(&tree[start..tree.len() - 1], &path, out);
            return;
        }
        if tree[i].kind != Kind::Ident {
            return;
        }
        path.push(tree[i].text);
        i += 1;
        if i == tree.len() {
            out.push((*path.last().unwrap(), path));
            return;
        }
        if starts_with(&tree[i..], &["as"])
            && i + 2 == tree.len()
            && tree[i + 1].kind == Kind::Ident
        {
            out.push((tree[i + 1].text, path));
            return;
        }
        if !tree[i].is("::") {
            return;
        }
        i += 1;
    }
}

fn imports<'a>(source: &[Token<'a>]) -> Vec<Import<'a>> {
    let mut out = Vec::new();
    let mut scope = Vec::new();
    let mut i = 0;
    while i < source.len() {
        if source[i].is("use") {
            if let Some(end) = source[i..].iter().position(|t| t.is(";")) {
                let end = i + end;
                let mut entries = Vec::new();
                // Only unconditional file-scope imports are supported. A cfg-disabled use or
                // macro token tree is not evidence of a live binding at the export.
                let before = if i > 0 && source[i - 1].is("pub") {
                    i - 1
                } else {
                    i
                };
                if scope.is_empty()
                    && (before == 0 || source[before - 1].is(";") || source[before - 1].is("}"))
                {
                    use_tree(&source[i + 1..end], &[], &mut entries);
                }
                out.extend(entries.into_iter().map(|(name, path)| Import {
                    name,
                    path,
                    scope: scope.clone(),
                    span: i..end + 1,
                }));
                i = end + 1;
                continue;
            }
        }
        if matches!(source[i].text, "{" | "(" | "[") && source[i].kind == Kind::Punct {
            scope.push(i);
        }
        if matches!(source[i].text, "}" | ")" | "]") && source[i].kind == Kind::Punct {
            scope.pop();
        }
        i += 1;
    }
    out
}

fn scope_at(source: &[Token], at: usize) -> Vec<usize> {
    let mut scope = Vec::new();
    for (i, t) in source.iter().enumerate().take(at) {
        if t.is("{") {
            scope.push(i);
        }
        if t.is("}") {
            scope.pop();
        }
    }
    scope
}

fn alias_is_standard(name: &str, at: usize, source: &[Token]) -> bool {
    let imports = imports(source);
    let scope = scope_at(source, at);
    let candidates: Vec<_> = imports.iter().filter(|i| i.name == name).collect();
    // Two imports with the same name, even in other scopes, require explicit qualification.
    if candidates.len() != 1 {
        return false;
    }
    let import = candidates[0];
    if scope.len() != 1
        || !import.scope.is_empty()
        || source.iter().enumerate().any(|(i, t)| {
            t.is("use")
                && source[i..]
                    .iter()
                    .take_while(|t| !t.is(";"))
                    .any(|t| t.is("*"))
        })
        || !standard_pointer(&import.path, source)
    {
        return false;
    }
    source.iter().enumerate().all(|(i, t)| {
        if !t.is(name) || import.span.contains(&i) {
            return true;
        }
        // Other uses must also be unqualified zero-argument calls in the import's scope.
        // This rejects parameter/let/pattern/type/macro bindings and local function declarations.
        starts_with(&source[i + 1..], &["(", ")"])
            && !i.checked_sub(1).is_some_and(|n| {
                source[n].kind == Kind::Ident || source[n].is("::") || source[n].is(".")
            })
            && scope_at(source, i).len() == 1
    })
}

fn null_pointer(value: &[Token], source: &[Token]) -> bool {
    let Some(open) = value.iter().position(|t| t.is("(")) else {
        return false;
    };
    if !starts_with(&value[open..], &["(", ")"]) || open + 2 != value.len() {
        return false; // No arguments, casts, generic calls or trailing expressions.
    }
    let mut path = &value[..open];
    if path.first().is_some_and(|t| t.is("::")) {
        path = &path[1..];
    }
    if path.is_empty()
        || path.len().is_multiple_of(2)
        || !path.iter().enumerate().all(|(i, t)| {
            if i % 2 == 0 {
                t.kind == Kind::Ident
            } else {
                t.is("::")
            }
        })
    {
        return false;
    }
    let names: Vec<_> = path.iter().step_by(2).map(|t| t.text).collect();
    if standard_pointer(&names, source) {
        return true;
    }
    if names.len() != 1 {
        return false;
    }
    let Some(at) = source.iter().position(|t| t.start == path[0].start) else {
        return false;
    };
    alias_is_standard(names[0], at, source)
}
