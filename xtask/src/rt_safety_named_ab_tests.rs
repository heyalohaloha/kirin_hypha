// INV-LC17: the named A/B before PRE / POST Blind. The same frozen trial plays PRE and POST by
// name, counts nothing as heard, and START BLIND begins a new anonymous trial of the same range
// whose first pass starts at the range start.

const TRIAL_H: &str = include_str!("../../juce_shell/src/local_blind/LocalBlindTrial.h");
const TRIAL_CPP: &str = include_str!("../../juce_shell/src/local_blind/LocalBlindTrial.cpp");
const COMMAND_H: &str = include_str!("../../juce_shell/src/local_blind/TrialCommandState.h");
const COMPONENT_CPP: &str = include_str!("../../juce_shell/src/HyphaLocalBlindComponent.cpp");
const PRESENTATION_CPP: &str = include_str!("../../juce_shell/src/HyphaLocalBlindPresentation.cpp");
const STEPS_CPP: &str = include_str!("../../juce_shell/src/HyphaLocalBlindSteps.cpp");
const EDITOR_CPP: &str = include_str!("../../juce_shell/src/PluginEditorLocalBlind.cpp");

// The body of the first function whose definition starts with signature (brace matched).
fn function_body<'a>(source: &'a str, signature: &str) -> &'a str {
    let start = source.find(signature).expect("the function exists");
    let open = start + source[start..].find('{').expect("the body opens");
    let mut depth = 0usize;
    for (offset, ch) in source[open..].char_indices() {
        match ch {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    return &source[open..=open + offset];
                }
            }
            _ => {}
        }
    }
    panic!("unbalanced body");
}

#[test]
fn named_ab_counts_nothing_and_blind_after_it_is_a_new_trial() {
    assert!(TRIAL_H.contains("namedOne = 5, namedTwo = 6"));
    let named = function_body(TRIAL_CPP, "bool LocalBlindTrial::startNamed");
    assert!(named.contains("gain.requiresLowerPostApproval && ! approve"));
    assert!(named.contains("issue (namedOne)"));
    let blind = function_body(TRIAL_CPP, "bool LocalBlindTrial::startBlind");
    let issue = blind
        .find("issue (one)")
        .expect("Blind begins with Source 1");
    for reset in [
        "heardOne.store (0",
        "heardTwo.store (0",
        "answered.store (TrialAnswer::none",
        "revealed.store (false",
    ] {
        let at = blind.find(reset).expect(reset);
        assert!(at < issue, "{reset} is published by the Blind command");
    }
    let render = function_body(TRIAL_CPP, "TrialOutput LocalBlindTrial::render");
    assert!(render.contains(
        "if (! named && requested != renderedCommand && isNamed (kind (renderedCommand)))"
    ));
    assert!(render.contains("if (completePass && ! named)"));
    assert!(
        render.contains("const bool pre = named ? mode == namedOne : (mode == one) == oneIsPre;")
    );
    // The render stays free of allocation, locks and waits like the rest of the trial.
    let code: String = render
        .lines()
        .map(|line| line.split_once("//").map_or(line, |(code, _)| code))
        .collect::<Vec<_>>()
        .join("\n");
    for forbidden in [
        "new ",
        "std::mutex",
        "lock_guard",
        "sleep",
        "malloc",
        "push_back",
    ] {
        assert!(
            !code.contains(forbidden),
            "the named A/B render must not contain {forbidden}"
        );
    }
    let view = function_body(TRIAL_CPP, "TrialView LocalBlindTrial::view");
    assert!(view.contains("result.canAnswer = ! result.named"));
}

#[test]
fn automatic_source_advance_is_bounded_and_cannot_replace_newer_control() {
    let render = function_body(TRIAL_CPP, "TrialOutput LocalBlindTrial::render");
    assert!(render.contains("command.advanceRendered (requested, two)"));
    assert!(!render.contains("issue ("));
    let advance = function_body(COMMAND_H, "bool advanceRendered");
    assert!(advance.contains("value.compare_exchange_strong (rendered, next (rendered, kind)"));
    for forbidden in [
        "while",
        "for (",
        "issue (",
        ".store (",
        "std::mutex",
        "sleep",
        "new ",
    ] {
        assert!(
            !advance.contains(forbidden),
            "RT advancement must not contain {forbidden}"
        );
    }
}

#[test]
fn named_ab_screen_names_sources_and_offers_the_entry_only_where_it_fits() {
    let resized = function_body(COMPONENT_CPP, "void Component::resized");
    assert!(resized.contains("namedButton.setVisible (current.phase == Phase::ready"));
    assert!(resized.contains("rowFits (actions.getWidth(), { &namedButton, &startButton })"));
    assert!(PRESENTATION_CPP.contains("\"PRE / POST NAMED A/B\""));
    assert!(PRESENTATION_CPP.contains("button->setEnabled (named || replayAvailable);"));
    assert!(PRESENTATION_CPP
        .contains("\"LOWER POST \" + juce::String (attenuation, 1) + \" dB & A/B\""));
    assert!(STEPS_CPP.contains("return view.trial.named ? Step::start : Step::listen;"));
    assert!(EDITOR_CPP.contains("processorRef.startLocalBlindProductNamed (approveLowerPost)"));
    assert!(EDITOR_CPP.contains("processorRef.startLocalBlindProductBlindFromNamed()"));
}
