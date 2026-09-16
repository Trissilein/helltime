import assert from "node:assert/strict";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

const projectRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));
const outputDir = mkdtempSync(join(tmpdir(), "helltime-schedule-"));
const tsc = join(projectRoot, "node_modules", "typescript", "bin", "tsc");
const require = createRequire(import.meta.url);

try {
  assert.ok(existsSync(tsc), "TypeScript compiler missing; run npm install first");
  const compile = spawnSync(process.execPath, [tsc, "--target", "ES2022", "--module", "commonjs", "--skipLibCheck", "--outDir", outputDir,
    join(projectRoot, "src/lib/helltides.ts"), join(projectRoot, "src/lib/helpers.ts"), join(projectRoot, "src/lib/types.ts")], { stdio: "inherit" });
  assert.equal(compile.status, 0, "schedule test TypeScript compilation failed");

  const { generateSchedule } = require(join(outputDir, "helltides.js"));
  const { findActiveOrNextHelltide, findDueReminderTimers } = require(join(outputDir, "helpers.js"));

  const now = Date.parse("2026-09-16T19:50:00Z");
  const schedule = generateSchedule(now);
  assert.deepEqual(generateSchedule(now), generateSchedule(now));
  assert.equal(schedule.helltide[0].startTime, "2026-09-16T19:00:00.000Z");
  assert.equal(schedule.legion[0].startTime, "2026-09-16T19:50:00.000Z");
  assert.equal(schedule.legion[1].timestamp - schedule.legion[0].timestamp, 25 * 60);
  assert.equal(schedule.world_boss[0].startTime, "2026-09-16T20:00:00.000Z");
  assert.equal(schedule.world_boss[1].timestamp - schedule.world_boss[0].timestamp, 3.5 * 60 * 60);
  assert.ok(schedule.helltide.at(-1).timestamp <= now / 1000 + 48 * 60 * 60);
  assert.ok(schedule.helltide.every((item) => item.id === item.timestamp));
  assert.equal(schedule.world_boss[0].boss, undefined);
  assert.equal(schedule.world_boss[0].zone, undefined);

  const active = findActiveOrNextHelltide(schedule.helltide, Date.parse("2026-09-16T19:54:59Z"));
  assert.equal(active?.active, true);
  assert.equal(active?.targetMs, Date.parse("2026-09-16T19:55:00Z"));
  const inactive = findActiveOrNextHelltide(schedule.helltide, Date.parse("2026-09-16T19:55:00Z"));
  assert.equal(inactive?.active, false);
  assert.equal(inactive?.item.startTime, "2026-09-16T20:00:00.000Z");

  const event = { startTime: "2026-09-16T21:00:00.000Z" };
  const caughtUp = findDueReminderTimers(
    event,
    [{ minutesBefore: 10 }, { minutesBefore: 8 }, { minutesBefore: 5 }],
    3,
    Date.parse("2026-09-16T20:56:00Z"),
    Date.parse("2026-09-16T20:49:00Z")
  );
  assert.equal(caughtUp.catchUp, true);
  assert.deepEqual(caughtUp.due.map((timer) => timer.index), [1, 2]);
  assert.deepEqual(findDueReminderTimers(event, [{ minutesBefore: 5 }], 1, Date.parse("2026-09-16T20:55:01Z"), Date.parse("2026-09-16T20:54:59Z")).due.map((timer) => timer.index), [0]);
  assert.deepEqual(findDueReminderTimers(event, [{ minutesBefore: 5 }], 1, Date.parse("2026-09-16T21:00:01Z"), Date.parse("2026-09-16T20:59:59Z")).due, []);
  assert.deepEqual(findDueReminderTimers(event, [{ minutesBefore: 5 }], 1, Date.parse("2026-09-16T20:54:00Z"), Date.parse("2026-09-16T20:55:00Z")).due, []);

  const dstSchedule = generateSchedule(Date.parse("2026-10-25T00:30:00Z"));
  assert.equal(dstSchedule.helltide[0].startTime, "2026-10-25T00:00:00.000Z");
  assert.equal(dstSchedule.helltide[1].timestamp - dstSchedule.helltide[0].timestamp, 3600);
  const yearBoundary = generateSchedule(Date.parse("2026-12-31T23:59:30Z"));
  assert.ok(yearBoundary.helltide.some((item) => item.startTime === "2027-01-01T00:00:00.000Z"));
  console.log("schedule tests passed");
} finally {
  rmSync(outputDir, { recursive: true, force: true });
}
