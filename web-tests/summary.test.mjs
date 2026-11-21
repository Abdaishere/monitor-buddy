// The closing summary of the installer: what works, what is left, what is optional.
import test from "node:test";
import assert from "node:assert/strict";
import { summaryItems } from "../web/js/form.js";
import { S } from "../web/js/strings.js";

const up = { fw: "1.0.0", wifi: { saved: true, ssid: "HomeNet", connected: true }, keys: { link: false, update: false } };

test("connected and not paired: Wi-Fi works, the computer link is an optional extra", () => {
  const s = summaryItems(up, "connected");
  assert.deepEqual(s.works, ["firmware", "wifiUp", "standalone"]);
  assert.deepEqual(s.left, []);
  assert.deepEqual(s.optional, ["linkNone", "card"]);
});

test("a paired Buddy lists the computer link under what works", () => {
  const s = summaryItems({ ...up, keys: { link: "0a1b2c3d", update: "4e5f6a7b" } }, "connected");
  assert.ok(s.works.includes("linkUp"));
  assert.ok(!s.optional.includes("linkNone"));
});

test("a failed join is left to do, whatever INFO still says", () => {
  assert.deepEqual(summaryItems(up, "wifiFailed").left, ["wifiDown"]);
  assert.deepEqual(summaryItems({ ...up, wifi: { saved: true, ssid: "HomeNet", connected: false } }, "saved").left, ["wifiDown"]);
});

test("no network yet, or the hotspot: Wi-Fi is left to do", () => {
  assert.deepEqual(summaryItems({ fw: "1.0.0", wifi: { saved: false, ssid: "" } }, "hotspot").left, ["wifiNone"]);
  assert.deepEqual(summaryItems({ fw: "1.0.0" }, "saved").left, ["wifiNone"]);
});

test("firmware without key support says nothing about being paired", () => {
  const s = summaryItems({ fw: "1.3.0", wifi: up.wifi }, "connected");
  assert.ok(!s.works.includes("linkUp"));
  assert.ok(s.optional.includes("linkNone"));
});

test("every summary key has a text", () => {
  for (const info of [up, { ...up, keys: { link: "x" } }, {}]) {
    for (const kind of ["connected", "wifiFailed", "hotspot", "saved"]) {
      const s = summaryItems(info, kind);
      for (const key of [...s.works, ...s.left, ...s.optional]) assert.ok(S.result.summary[key], key);
    }
  }
});
