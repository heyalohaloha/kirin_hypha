pub(crate) fn parse_line(line: &str) -> Result<Vec<String>, String> {
    let mut fields = Vec::new();
    let mut field = String::new();
    let mut characters = line.chars().peekable();
    let mut quoted = false;
    let mut closed_quote = false;
    while let Some(character) = characters.next() {
        if quoted {
            if character == '"' {
                if characters.peek() == Some(&'"') {
                    characters.next();
                    field.push('"');
                } else {
                    quoted = false;
                    closed_quote = true;
                }
            } else if character == '\r' || character == '\n' {
                return Err("embedded newline is forbidden".to_string());
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

pub(crate) fn encode(value: &str) -> String {
    if value.contains([',', '"', '\r', '\n']) {
        format!("\"{}\"", value.replace('"', "\"\""))
    } else {
        value.to_string()
    }
}

pub(crate) fn parse_seconds_micros(value: &str) -> Result<i64, String> {
    if value.is_empty() || value.bytes().any(|byte| byte.is_ascii_whitespace()) {
        return Err("seconds must be a nonempty canonical decimal".to_string());
    }
    let (negative, unsigned) = value
        .strip_prefix('-')
        .map_or((false, value), |rest| (true, rest));
    if unsigned.is_empty() {
        return Err("invalid seconds".to_string());
    }
    let mut parts = unsigned.split('.');
    let whole = parts.next().unwrap();
    let fraction = parts.next();
    if parts.next().is_some()
        || whole.is_empty()
        || !whole.bytes().all(|byte| byte.is_ascii_digit())
    {
        return Err("invalid seconds".to_string());
    }
    let fraction = fraction.unwrap_or("");
    if fraction.len() > 6 || !fraction.bytes().all(|byte| byte.is_ascii_digit()) {
        return Err("seconds may have at most six decimal places".to_string());
    }
    let whole = whole
        .parse::<i64>()
        .map_err(|_| "seconds overflow".to_string())?;
    let mut fractional = fraction
        .parse::<i64>()
        .map_err(|_| "invalid fractional seconds".to_string())?;
    for _ in fraction.len()..6 {
        fractional *= 10;
    }
    let magnitude = whole
        .checked_mul(1_000_000)
        .and_then(|value| value.checked_add(fractional))
        .ok_or("seconds overflow")?;
    if negative {
        magnitude
            .checked_neg()
            .ok_or("seconds overflow".to_string())
    } else {
        Ok(magnitude)
    }
}

pub(crate) fn parse_positive_duration_micros(value: &str) -> Result<u64, String> {
    if value.is_empty() || value.bytes().any(|byte| byte.is_ascii_whitespace()) {
        return Err("duration must be a nonempty canonical decimal".to_string());
    }
    let mut parts = value.split('.');
    let whole = parts.next().unwrap();
    let fraction = parts.next().unwrap_or("");
    if parts.next().is_some()
        || whole.is_empty()
        || !whole.bytes().all(|byte| byte.is_ascii_digit())
        || !fraction.bytes().all(|byte| byte.is_ascii_digit())
    {
        return Err("invalid duration".to_string());
    }
    let whole = whole
        .parse::<u64>()
        .map_err(|_| "duration overflow".to_string())?;
    let first_six = &fraction[..fraction.len().min(6)];
    let mut fractional = if first_six.is_empty() {
        0
    } else {
        first_six
            .parse::<u64>()
            .map_err(|_| "invalid duration fraction".to_string())?
    };
    for _ in first_six.len()..6 {
        fractional *= 10;
    }
    let rounds_up = fraction
        .as_bytes()
        .get(6)
        .is_some_and(|digit| *digit >= b'5');
    let micros = whole
        .checked_mul(1_000_000)
        .and_then(|value| value.checked_add(fractional))
        .and_then(|value| value.checked_add(u64::from(rounds_up)))
        .ok_or("duration overflow")?;
    if micros == 0 {
        Err("duration must be positive".to_string())
    } else {
        Ok(micros)
    }
}

pub(crate) fn format_micros(value: i64) -> String {
    let negative = value < 0;
    let magnitude = value.unsigned_abs();
    format!(
        "{}{whole}.{fraction:06}",
        if negative { "-" } else { "" },
        whole = magnitude / 1_000_000,
        fraction = magnitude % 1_000_000,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn csv_round_trip_and_decimal_contract_are_strict() {
        let value = "a, \"quoted\" value";
        assert_eq!(parse_line(&encode(value)).unwrap(), [value]);
        assert_eq!(parse_seconds_micros("1.025").unwrap(), 1_025_000);
        assert_eq!(parse_seconds_micros("-0.002000").unwrap(), -2_000);
        assert_eq!(format_micros(-2_000), "-0.002000");
        assert_eq!(
            parse_positive_duration_micros("3.255124716553288").unwrap(),
            3_255_125
        );
        for invalid in ["", " 1", "+1", "1e-3", "1.0000001", "1."] {
            assert!(parse_seconds_micros(invalid).is_err(), "{invalid}");
        }
    }
}
