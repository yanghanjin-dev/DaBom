import fs from "fs";
import path from "path";
import os from "os";
import { DatabaseSync } from "node:sqlite";

const roots = [
  path.join(os.homedir(), "AppData", "Roaming", "Cursor"),
  path.join(os.homedir(), "AppData", "Local", "Cursor"),
  path.join(os.homedir(), ".cursor"),
];
const install = path.join(os.homedir(), "AppData", "Local", "Programs", "cursor");

const textPat =
  /uiFontSize|UIFontSize|ui_font_size|agentsUiFont|agentUiFont|glass\.[A-Za-z0-9_.]*[Ff]ont|appearance\.[A-Za-z0-9_.]*[Ff]ont|codeFontSize|ui\.font\.size|FontSize/i;

function walk(dir, out = []) {
  let ents;
  try {
    ents = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return out;
  }
  for (const e of ents) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else out.push(p);
  }
  return out;
}

function scanSqlite(file) {
  let db;
  try {
    db = new DatabaseSync(file, { readOnly: true });
  } catch (e) {
    console.log("SQLERR", file, e.message);
    return;
  }
  let tables;
  try {
    tables = db.prepare("SELECT name FROM sqlite_master WHERE type='table'").all();
  } catch (e) {
    console.log("SQLERR tables", file, e.message);
    db.close();
    return;
  }
  for (const { name: t } of tables) {
    try {
      const rows = db.prepare(`SELECT * FROM [${t}]`).all();
      for (const row of rows) {
        const blob = Object.entries(row)
          .map(([k, v]) => `${k}=${v}`)
          .join(" | ");
        if (textPat.test(blob)) {
          console.log("SQLHIT", file, "table=", t);
          console.log("  ROW:", blob.slice(0, 900).replace(/\n/g, " "));
        }
      }
    } catch {
      /* ignore */
    }
  }
  db.close();
}

function scanBytes(file) {
  let data;
  try {
    data = fs.readFileSync(file);
  } catch {
    return;
  }
  if (!textPat.test(data.toString("latin1"))) return;
  console.log("BITHIT", file, "size=", data.length);
  const s = data.toString("utf8");
  const re =
    /.{0,50}(uiFontSize|UIFontSize|uiFont|FontSize|fontSize|glass\.[A-Za-z0-9_.]*[Ff]ont|appearance\.[A-Za-z0-9_.]*|codeFontSize)[^\0]{0,120}/gi;
  let m;
  let n = 0;
  while ((m = re.exec(s)) && n < 12) {
    console.log(" ", m[0].replace(/[\0-\x1f]/g, " ").slice(0, 240));
    n++;
  }
}

console.log("=== DATA ===");
for (const root of roots) {
  if (!fs.existsSync(root)) {
    console.log("MISSING", root);
    continue;
  }
  console.log("WALK", root);
  for (const file of walk(root)) {
    let sz = 0;
    try {
      sz = fs.statSync(file).size;
    } catch {
      continue;
    }
    if (sz === 0 || sz > 80_000_000) continue;
    const low = file.toLowerCase();
    if (low.endsWith(".vscdb") || low.endsWith(".sqlite") || low.endsWith(".db")) {
      scanSqlite(file);
    } else if (
      low.endsWith(".json") ||
      low.endsWith(".log") ||
      low.endsWith(".txt") ||
      low.endsWith(".vscdb-wal") ||
      /prefer/i.test(path.basename(file)) ||
      ["local state", "preferences"].includes(path.basename(file).toLowerCase())
    ) {
      scanBytes(file);
    }
  }
}

console.log("=== INSTALL KEYS ===");
const appRoot = path.join(install, "resources", "app");
const keyRe =
  /uiFontSize|UI Font Size|glass\.[A-Za-z0-9_.]*[Ff]ont[A-Za-z0-9_.]*|appearance\.[A-Za-z0-9_.]*[Ff]ont|defaultUiFontSize|UI_FONT_SIZE|uiFontSizeMinimum|codeFontSize/gi;
let hits = 0;
for (const file of walk(appRoot)) {
  if (!/\.(js|json|css)$/i.test(file)) continue;
  let sz = 0;
  try {
    sz = fs.statSync(file).size;
  } catch {
    continue;
  }
  if (sz === 0 || sz > 40_000_000) continue;
  let data;
  try {
    data = fs.readFileSync(file, "utf8");
  } catch {
    continue;
  }
  if (!keyRe.test(data)) continue;
  keyRe.lastIndex = 0;
  const uniq = new Set();
  let m;
  while ((m = keyRe.exec(data))) uniq.add(m[0]);
  if (uniq.size) {
    hits++;
    console.log("INSTHIT", file);
    console.log("  keys:", [...uniq].join(", "));
    // nearby context for uiFontSize
    const ctx = /.{0,100}uiFontSize.{0,150}/gi;
    let c;
    let cn = 0;
    while ((c = ctx.exec(data)) && cn < 4) {
      console.log("  ctx:", c[0].replace(/\s+/g, " ").slice(0, 280));
      cn++;
    }
  }
  if (hits >= 40) break;
}
console.log("install file hits", hits);
console.log("DONE");
