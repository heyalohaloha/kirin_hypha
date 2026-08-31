pub(crate) fn parse_csv_line(line: &str) -> Result<Vec<String>, String> {
    let mut fields = Vec::new();
    let mut field = String::new();
    let mut chars = line.chars().peekable();
    let mut quoted = false;
    let mut closed_quote = false;
    while let Some(character) = chars.next() {
        if quoted {
            if character == '"' {
                if chars.peek() == Some(&'"') {
                    chars.next();
                    field.push('"');
                } else {
                    quoted = false;
                    closed_quote = true;
                }
            } else {
                field.push(character);
            }
        } else if character == ',' {
            fields.push(std::mem::take(&mut field));
            closed_quote = false;
        } else if character == '"' && field.is_empty() && !closed_quote {
            quoted = true;
        } else if closed_quote || character == '"' {
            return Err("invalid CSV quoting".to_string());
        } else {
            field.push(character);
        }
    }
    if quoted {
        return Err("unterminated CSV quote".to_string());
    }
    fields.push(field);
    Ok(fields)
}

pub(crate) fn encode_csv_field(value: &str) -> String {
    if value.contains([',', '"', '\r', '\n']) {
        format!("\"{}\"", value.replace('"', "\"\""))
    } else {
        value.to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn csv_round_trip_handles_quotes_and_commas() {
        let fields = ["plain", "comma,value", "a \"quote\""];
        let line = fields
            .iter()
            .map(|field| encode_csv_field(field))
            .collect::<Vec<_>>()
            .join(",");
        assert_eq!(parse_csv_line(&line).unwrap(), fields);
        assert!(parse_csv_line("\"unterminated").is_err());
    }
}
