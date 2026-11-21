// The personal touches in the installer: the greeting sample, the birthday
// fields, the set-up day, and the settings rules behind them.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  DEFAULTS, PERSONAL_KEYS, greetingPreview, birthdayText, birthdayParts, daysInMonth, sinceText, stampSince,
  fromBoard, newFormState, formToRequest,
} from "../web/js/form.js";
import { S } from "../web/js/strings.js";
import { validateConfig, mergeConfig, pathsForCfg, supportedPaths, filterSupported } from "../web/js/settings.js";

const good = (patch) => assert.equal(validateConfig(mergeConfig(DEFAULTS, patch)).ok, true, JSON.stringify(patch));
const bad = (patch, key) => {
  const r = validateConfig(mergeConfig(DEFAULTS, patch));
  assert.equal(r.ok, false, JSON.stringify(patch));
  assert.ok(Object.keys(r.errors).some((k) => k === key || k.startsWith(`${key}.`)), `${key} in ${JSON.stringify(r.errors)}`);
};

// The same lines as test/test_persona (the firmware's personaGreeting).
test("the greeting sample is what the firmware will show", () => {
  const g = (greeting, greeting_text, name, pick) => greetingPreview({ greeting, greeting_text, name }, pick);
  assert.equal(g("hello", "", "Sam", 0), "HELLO SAM!");
  assert.equal(g("yo", "", "Sam", 0), "YO SAM!");
  assert.equal(g("salam", "", "Sam K.", 0), "SALAM SAM K.!");
  assert.equal(g("hello", "", "", 0), "HELLO!");
  assert.equal(g("custom", "Ahoy, captain", "Sam", 0), "Ahoy, captain");
  assert.equal(g("custom", "", "Sam", 1), "HELLO!");
  assert.equal(g("mix", "", "", 0), "WHAT'S UP?");
  assert.equal(g("mix", "", "", 5), "WHAT'S UP?");
  assert.equal(g("mix", "", "Sam", 5), "HEY SAM!");
  assert.equal(g(undefined, undefined, undefined, 4), "WHO'S THERE?");
});

test("the firmware's greeting lines are the form's", () => {
  const cpp = readFileSync(new URL("../lib/persona/persona.cpp", import.meta.url), "utf8");
  for (const line of ["WHAT'S UP?", "HELLO!", "YES?", "HEY THERE!", "WHO'S THERE?"]) assert.ok(cpp.includes(`"${line}"`), line);
  for (const word of ["HELLO", "YO", "SALAM", "HEY"]) assert.ok(cpp.includes(`withName("${word}"`), word);
});

test("every greeting fits the 26 characters of a caption", () => {
  for (const greeting of ["mix", "hello", "yo", "salam", "custom"]) {
    for (let pick = 0; pick < 6; pick++) {
      const text = greetingPreview({ greeting, greeting_text: "x".repeat(40), name: "ABCDEFGHIJKLMNOP" }, pick);
      assert.ok(text.length <= 26, text);
    }
  }
});

test("birthday: month and day to MM-DD and back", () => {
  assert.equal(birthdayText(3, 14), "03-14");
  assert.equal(birthdayText("11", "8"), "11-08");
  assert.equal(birthdayText(2, 29), "02-29");
  assert.equal(birthdayText(2, 30), "");
  assert.equal(birthdayText(4, 31), "");
  assert.equal(birthdayText("", 5), "");
  assert.equal(birthdayText(13, 1), "");
  assert.deepEqual(birthdayParts("03-14"), { month: 3, day: 14 });
  assert.deepEqual(birthdayParts(""), { month: "", day: "" });
  assert.deepEqual(birthdayParts("02-30"), { month: "", day: "" });
  assert.deepEqual(birthdayParts("3 March"), { month: "", day: "" });
  assert.equal(daysInMonth(2), 29);
  assert.equal(daysInMonth(4), 30);
  assert.equal(daysInMonth(""), 31);
});

test("the set-up day is stamped once, in local time, and only for firmware that knows it", () => {
  assert.equal(sinceText(new Date(2026, 10, 20, 23, 59)), "2026-11-20");
  assert.equal(sinceText(new Date(2027, 0, 5)), "2027-01-05");
  const f = newFormState(DEFAULTS, { flow: "change", supported: supportedPaths(DEFAULTS) });
  stampSince(f, new Date(2026, 10, 20));
  assert.equal(f.config.since, "2026-11-20");
  stampSince(f, new Date(2027, 5, 1));
  assert.equal(f.config.since, "2026-11-20");
  const old = newFormState(DEFAULTS, { flow: "change", supported: new Set(pathsForCfg(1)) });
  stampSince(old, new Date(2026, 10, 20));
  assert.equal(old.config.since, "");
});

test("the defaults are valid and switch every personal touch off", () => {
  good({});
  assert.equal(DEFAULTS.name, "");
  assert.equal(DEFAULTS.buddy_name, "");
  assert.equal(DEFAULTS.greeting, "mix");
  assert.equal(DEFAULTS.birthday, "");
  assert.equal(DEFAULTS.break_min, 0);
  assert.equal(DEFAULTS.water, false);
  assert.equal(DEFAULTS.message, "");
  assert.deepEqual({ ...DEFAULTS.work }, { start: 9, end: 17, lunch: 12 });
});

test("personal settings are checked like the firmware checks them", () => {
  good({ buddy_name: "Pixel", name: "Sam K.", greeting: "salam", greeting_text: "Ahoy, captain?", work: { start: 8, end: 24, lunch: 13 },
    weekend: "fri-sat", birthday: "02-29", since: "2026-11-20", break_min: 120, water: true, message: "You've got this" });
  bad({ buddy_name: "a".repeat(13) }, "buddy_name");
  bad({ buddy_name: "<b>" }, "buddy_name");
  bad({ greeting: "howdy" }, "greeting");
  bad({ greeting_text: "x".repeat(27) }, "greeting_text");
  bad({ greeting_text: 'say "hi"' }, "greeting_text");
  bad({ work: { start: 17, end: 9 } }, "work");
  bad({ work: { start: 9, end: 9 } }, "work");
  bad({ work: { start: 24 } }, "work");
  bad({ work: { end: 0 } }, "work");
  bad({ weekend: "sun" }, "weekend");
  bad({ birthday: "3 March" }, "birthday");
  bad({ since: "20 Nov 2026" }, "since");
  bad({ break_min: 121 }, "break_min");
  bad({ break_min: -1 }, "break_min");
  bad({ water: "yes" }, "water");
  bad({ message: "x".repeat(27) }, "message");
  bad({ message: "a\\b" }, "message");
  good({ face: "cat" });
  bad({ face: "dog" }, "face");
});

test("a board from before the personal settings: its config loads, and nothing personal is sent to it", () => {
  const old = structuredClone(DEFAULTS);
  for (const key of PERSONAL_KEYS) delete old[key];
  const shown = fromBoard(old);
  for (const key of PERSONAL_KEYS) assert.deepEqual(shown[key], key === "work" ? { ...DEFAULTS.work } : DEFAULTS[key], key);
  assert.equal(validateConfig(shown).ok, true);
  const sent = filterSupported(shown, supportedPaths(old));
  for (const key of PERSONAL_KEYS) assert.equal(Object.hasOwn(sent, key), false, key);
  const f = newFormState(shown, { flow: "change", supported: supportedPaths(old) });
  f.config.buddy_name = "Pixel";
  assert.equal(Object.hasOwn(formToRequest(f).config, "buddy_name"), false);
});

test("the form's work object is its own copy, not the frozen defaults", () => {
  const shown = fromBoard({});
  shown.work.start = 7;
  assert.equal(DEFAULTS.work.start, 9);
});

test("every face style has a name and a picture drawn by the firmware", () => {
  for (const name of ["classic", "cat", "robot", "minimal"]) {
    assert.ok(S.form.faceNames[name], name);
    const png = readFileSync(new URL(`../web/img/face-${name}.png`, import.meta.url));
    assert.equal(png.readUInt32BE(16), 320, name);   // the screen's size: a real frame
    assert.equal(png.readUInt32BE(20), 172, name);
  }
  assert.equal(DEFAULTS.face, "classic");
});

test("the wave is asked for only from firmware that knows the face setting", () => {
  const app = readFileSync(new URL("../web/js/app.js", import.meta.url), "utf8");
  assert.match(app, /fieldState\(app\.form, "face"\) !== "on"\) return;\s+app\.bp\.command\("STATE", \{ wave: true \}/);
});
