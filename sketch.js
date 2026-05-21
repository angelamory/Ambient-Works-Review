// ── Config ──────────────────────────────────────────────────────────────────
let deviceIp     = "ambient.local";
const UPDATE_MS  = 2000;
const MAX_HIST   = 120;

// ── State ────────────────────────────────────────────────────────────────────
let ipInput, connectButton;
let sensorData   = null;
let lastUpdate   = 0;
let historyData  = [];
let connStatus   = "connecting"; // "connecting" | "connected" | "error"

// ── API history mode ─────────────────────────────────────────────────────────
let historyMode        = "live"; // "live" | "api"
let apiHistory         = [];
let apiHistoryStatus   = "idle"; // "idle" | "loading" | "loaded" | "error"
let _btnLiveRect       = null;
let _btnApiRect        = null;

// ── Palette ──────────────────────────────────────────────────────────────────
const C = {
  bg:     [11,  13,  18],
  surf:   [19,  22,  30],
  card:   [26,  30,  42],
  border: [42,  48,  66],
  text:   [232, 238, 255],
  muted:  [105, 115, 148],
  pm:     [56,  145, 255],
  co2:    [255, 158,  40],
  temp:   [255,  75,  90],
  humi:   [40,  200, 210],
  voc:    [165,  85, 240],
  nox:    [255, 205,  60],
  pm1:    [85,  210, 150],
  ok:     [65,  196, 108],
  warn:   [255, 198,  52],
  danger: [255,  72,  72],
};

// ── Setup ────────────────────────────────────────────────────────────────────
function setup() {
  createCanvas(windowWidth, windowHeight);

  // HTML input bar (top-right, fixed)
  let bar = createDiv('');
  bar.id = 'topBar';
  bar.style('position', 'fixed');
  bar.style('top', '14px');
  bar.style('right', '17px');
  bar.style('display', 'flex');
  bar.style('align-items', 'center');
  bar.style('gap', '8px');
  bar.style('z-index', '9999');

  let lbl = createSpan('Device IP');
  lbl.parent(bar);
  lbl.style('color', '#6e78a0');
  lbl.style('font-size', '12px');
  lbl.style('font-family', 'system-ui, sans-serif');

  ipInput = createInput(deviceIp);
  ipInput.parent(bar);
  ipInput.attribute('placeholder', '192.168.x.x');
  ipInput.style('width', '148px');
  ipInput.style('padding', '5px 10px');
  ipInput.style('background', '#151820');
  ipInput.style('color', '#dde2f5');
  ipInput.style('border', '1px solid #2a2f44');
  ipInput.style('border-radius', '5px');
  ipInput.style('font-family', '"SF Mono", Consolas, monospace');
  ipInput.style('font-size', '12px');
  ipInput.style('outline', 'none');

  connectButton = createButton('Connect');
  connectButton.parent(bar);
  connectButton.mousePressed(doConnect);
  connectButton.style('padding', '5px 13px');
  connectButton.style('background', '#FF5C00');
  connectButton.style('color', '#fff');
  connectButton.style('border', 'none');
  connectButton.style('border-radius', '5px');
  connectButton.style('font-family', 'system-ui, sans-serif');
  connectButton.style('font-size', '12px');
  connectButton.style('font-weight', '600');
  connectButton.style('cursor', 'pointer');

  downloadButton = createButton('Download Data');
  downloadButton.parent(bar);
  downloadButton.mousePressed(doDownload);
  downloadButton.style('padding', '5px 13px');
  downloadButton.style('background', '#FF5C00');
  downloadButton.style('color', '#fff');
  downloadButton.style('border', 'none');
  downloadButton.style('border-radius', '5px');
  downloadButton.style('font-family', 'system-ui, sans-serif');
  downloadButton.style('font-size', '12px');
  downloadButton.style('font-weight', '600');
  downloadButton.style('cursor', 'pointer');

  

  fetchData();
}

function doDownload() {
  window.open(`http://${deviceIp}/api/history/csv`, '_blank');
}

function doConnect() {
  deviceIp  = ipInput.value().trim();
  historyData = [];
  sensorData  = null;
  connStatus  = "connecting";
  apiHistory  = [];
  apiHistoryStatus = "idle";
  fetchData();
}

function fetchData() {
  let base = deviceIp.startsWith("http") ? deviceIp.trim() : `http://${deviceIp.trim()}`;
  let url  = base.replace(/\/+$/, '') + '/api';

  fetch(url)
    .then(r => r.json())
    .then(res => {
      sensorData = res;
      connStatus = "connected";
      historyData.push({
        time:     new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
        pm2p5:    res.pm2p5       ?? 0,
        co2:      res.co2         ?? 0,
        temp:     res.temperature ?? 0,
        humidity: res.humidity    ?? 0,
        voc:      res.vocIndex    ?? 0,
        nox:      res.noxIndex    ?? 0,
        pm1:      res.pm1p0       ?? 0,
      });
      if (historyData.length > MAX_HIST) historyData.shift();
    })
    .catch(() => { connStatus = "error"; });
}

function fetchApiHistory() {
  apiHistoryStatus = "loading";
  let base = deviceIp.startsWith("http") ? deviceIp.trim() : `http://${deviceIp.trim()}`;
  let url  = base.replace(/\/+$/, '') + '/api/history';

  fetch(url)
    .then(r => r.json())
    .then(data => {
      apiHistory = data.map(entry => ({
        time:     new Date(entry.timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
        pm2p5:    entry.pm2p5       ?? 0,
        co2:      entry.co2         ?? 0,
        temp:     entry.temperature ?? 0,
        humidity: entry.humidity    ?? 0,
        voc:      entry.vocIndex    ?? 0,
        nox:      entry.noxIndex    ?? 0,
        pm1:      entry.pm1p0       ?? 0,
      }));
      apiHistoryStatus = "loaded";
    })
    .catch(() => { apiHistoryStatus = "error"; });
}

// ── Draw loop ────────────────────────────────────────────────────────────────
function draw() {
  background(...C.bg);

  if (millis() - lastUpdate > UPDATE_MS) {
    lastUpdate = millis();
    fetchData();
  }

  drawHeader();
  drawCards();
  drawGraph();
}


// ── Header ───────────────────────────────────────────────────────────────────
function drawHeader() {
  noStroke();
  fill(...C.surf);
  rect(0, 0, width, 56);
  stroke(...C.border);
  strokeWeight(1);
  line(0, 56, width, 56);
  noStroke();

  // Brand dot
  // fill("#FF5C00");
  // circle(22, 27, 9);
  push();
  translate(17, 16);
  drawLogo();
  pop();

  // Title
  fill(...C.text);
  textSize(16);
  textAlign(LEFT, CENTER);
  textStyle(BOLD);
  text("Ambient", 48, 28);
  textStyle(NORMAL);
  fill(...C.muted);
  textSize(12);
  text("Air Quality Monitor for Education", 120, 28);

  // Updated timestamp (center)
  if (sensorData && sensorData.timestamp) {
    fill(...C.muted);
    textAlign(CENTER, CENTER);
    textSize(11);
    text("Updated: " + sensorData.timestamp, width / 2, 20);
  }

  // Connection status (right side, before the HTML input)
  let sc = connStatus === "connected" ? C.ok
         : connStatus === "error"     ? C.danger : C.warn;
  let sl = connStatus === "connected" ? "Live"
         : connStatus === "error"     ? "Connection error" : "Connecting…";
  fill(...sc);
  circle((width / 2) - 8, 38, 7);
  textSize(12);
  textAlign(LEFT, CENTER);
  text(sl, width / 2, 40);

}

// ── Cards ────────────────────────────────────────────────────────────────────
function drawCards() {
  const m      = 16;
  const startY = 72;
  const ch     = 130;
  const totalW = width - m * 2;

  // Row 1 — 4 cards
  let n1  = 4;
  let cw1 = (totalW - m * (n1 - 1)) / n1;
  let row1 = [
    { label: "PM2.5",       val: fmt(sensorData?.pm2p5),          unit: "µg/m³", col: C.pm,   badge: pmAQI(sensorData?.pm2p5)  },
    { label: "CO₂",         val: fmt(sensorData?.co2, 0),         unit: "ppm",   col: C.co2,  badge: co2Level(sensorData?.co2) },
    { label: "Temperature", val: fmt(sensorData?.temperature),    unit: "°C",    col: C.temp, badge: null                      },
    { label: "Humidity",    val: fmt(sensorData?.humidity),       unit: "%",     col: C.humi, badge: null                      },
  ];
  for (let i = 0; i < row1.length; i++) {
    drawCard(m + i * (cw1 + m), startY, cw1, ch, row1[i]);
  }

  // Row 2 — 3 cards
  let n2   = 3;
  let cw2  = (totalW - m * (n2 - 1)) / n2;
  let row2Y = startY + ch + m;
  let row2 = [
    { label: "VOC Index", val: fmt(sensorData?.vocIndex, 0), unit: "idx",   col: C.voc },
    { label: "NOx Index", val: fmt(sensorData?.noxIndex, 0), unit: "idx",   col: C.nox },
    { label: "PM1.0",     val: fmt(sensorData?.pm1p0),       unit: "µg/m³", col: C.pm1 },
  ];
  for (let i = 0; i < row2.length; i++) {
    drawCard(m + i * (cw2 + m), row2Y, cw2, ch, row2[i]);
  }
}

function drawCard(x, y, w, h, d) {
  noStroke();
  // Drop shadow
  fill(0, 0, 0, 50);
  rect(x + 2, y + 4, w, h, 10);
  // Background
  fill(...C.card);
  rect(x, y, w, h, 10);
  // Top accent bar
  fill(...d.col);
  rect(x, y, w, 3, 10, 10, 0, 0);

  // Label
  fill(...C.muted);
  textSize(10);
  textAlign(LEFT, TOP);
  textStyle(NORMAL);
  text(d.label.toUpperCase(), x + 14, y + 16);

  // Value
  let isPlaceholder = (d.val === "--");
  if (isPlaceholder) fill(...C.muted); else fill(...C.text);
  textSize(38);
  textAlign(LEFT, BASELINE);
  let valStr = String(d.val);
  let baseY  = y + h - 28;
  text(valStr, x + 14, baseY);

  // Unit (same baseline, smaller)
  let vw = textWidth(valStr);    // measured at textSize 38
  textSize(13);
  fill(...C.muted);
  text(d.unit, x + 14 + vw + 5, baseY - 3);

  // AQI / level badge
  if (d.badge) {
    fill(...d.badge.color, 35);
    noStroke();
    rect(x + w - 76, y + h - 30, 64, 18, 5);
    fill(...d.badge.color);
    textSize(9);
    textAlign(CENTER, CENTER);
    text(d.badge.label.toUpperCase(), x + w - 44, y + h - 21);
  }
}

// ── Graph ────────────────────────────────────────────────────────────────────
function drawGraph() {
  const m      = 16;
  const ch     = 130;
  const startY = 72;
  const row2Y  = startY + ch + m;
  const x      = m;
  const y      = row2Y + ch + m;
  const w      = width - m * 2;
  const h      = height - y - m;

  if (h < 80) return;

  noStroke();
  fill(...C.card);
  rect(x, y, w, h, 10);

  const pad = { top: 38, right: 16, bottom: 32, left: 16 };
  let gx = x + pad.left,  gy = y + pad.top;
  let gw = w - pad.left - pad.right,  gh = h - pad.top - pad.bottom;

  // ── Mode toggle buttons ────────────────────────────────────────────────────
  const BH = 20, BLW = 44, BAW = 92, BGAP = 5;
  let blX = x + w - 16 - BAW - BGAP - BLW;
  let baX = blX + BLW + BGAP;
  let bY  = y + 9;
  _btnLiveRect = { x: blX, y: bY, w: BLW, h: BH };
  _btnApiRect  = { x: baX, y: bY, w: BAW, h: BH };

  let liveActive = historyMode === "live";
  let apiActive  = historyMode === "api";

  fill(...(liveActive ? [255,92,0] : C.surf));
  stroke(...C.border); strokeWeight(1);
  rect(blX, bY, BLW, BH, 4);
  noStroke();
  fill(...(liveActive ? [255,255,255] : C.muted));
  textSize(9); textAlign(CENTER, CENTER);
  text("Live", blX + BLW / 2, bY + BH / 2);

  fill(...(apiActive ? [255,92,0] : C.surf));
  stroke(...C.border); strokeWeight(1);
  rect(baX, bY, BAW, BH, 4);
  noStroke();
  fill(...(apiActive ? [255,255,255] : C.muted));
  textSize(9); textAlign(CENTER, CENTER);
  text("API History", baX + BAW / 2, bY + BH / 2);

  // Section label
  fill(...C.muted);
  textSize(10);
  textStyle(NORMAL);
  textAlign(LEFT, TOP);
  let labelSuffix = (historyMode === "api" && apiHistoryStatus === "loaded")
    ? `  •  ${apiHistory.length.toLocaleString()} readings` : "";
  text("HISTORY — ALL SENSORS" + labelSuffix, x + 14, y + 13);

  // ── Select active dataset ─────────────────────────────────────────────────
  let activeData, activeN;
  if (historyMode === "api") {
    if (apiHistoryStatus === "loading") {
      fill(...C.muted); textSize(13); textAlign(CENTER, CENTER);
      text("Loading API history…", x + w / 2, y + h / 2);
      return;
    }
    if (apiHistoryStatus === "error") {
      fill(...C.danger); textSize(13); textAlign(CENTER, CENTER);
      text("Failed to load API history — click the button to retry", x + w / 2, y + h / 2);
      return;
    }
    if (apiHistoryStatus === "idle") {
      fill(...C.muted); textSize(13); textAlign(CENTER, CENTER);
      text("Click \"API History\" to load", x + w / 2, y + h / 2);
      return;
    }
    activeData = apiHistory;
    activeN    = Math.max(activeData.length - 1, 1);
  } else {
    activeData = historyData;
    activeN    = MAX_HIST - 1;
  }

  if (activeData.length < 2) {
    fill(...C.muted);
    textSize(13);
    textAlign(CENTER, CENTER);
    text("Collecting data…", x + w / 2, y + h / 2);
    return;
  }

  // Value ranges
  let maxPM  = Math.max(...activeData.map(d => d.pm2p5));
  let maxCO2 = Math.max(...activeData.map(d => d.co2));
  if (maxPM  < 10)  maxPM  = 10;
  if (maxCO2 < 500) maxCO2 = 500;
  let pmCeil  = Math.ceil(maxPM  * 1.25 / 5)  * 5;
  let co2Ceil = Math.ceil(maxCO2 * 1.15 / 50) * 50;

  // Grid lines
  let lines = 4;
  for (let i = 0; i <= lines; i++) {
    let ly = map(i, 0, lines, gy + gh, gy);
    stroke(...C.border);
    strokeWeight(1);
    drawingContext.setLineDash([4, 6]);
    line(gx, ly, gx + gw, ly);
    drawingContext.setLineDash([]);
    noStroke();
  }

  // CO₂ filled area
  fill(...C.co2, 28);
  noStroke();
  beginShape();
  vertex(gx, gy + gh);
  for (let i = 0; i < activeData.length; i++) {
    vertex(
      map(i, 0, activeN, gx, gx + gw),
      map(activeData[i].co2, 0, co2Ceil, gy + gh, gy)
    );
  }
  vertex(map(activeData.length - 1, 0, activeN, gx, gx + gw), gy + gh);
  endShape(CLOSE);

  // CO₂ line
  stroke(...C.co2);
  strokeWeight(2);
  noFill();
  beginShape();
  for (let i = 0; i < activeData.length; i++) {
    vertex(
      map(i, 0, activeN, gx, gx + gw),
      map(activeData[i].co2, 0, co2Ceil, gy + gh, gy)
    );
  }
  endShape();

  // PM2.5 filled area
  fill(...C.pm, 40);
  noStroke();
  beginShape();
  vertex(gx, gy + gh);
  for (let i = 0; i < activeData.length; i++) {
    vertex(
      map(i, 0, activeN, gx, gx + gw),
      map(activeData[i].pm2p5, 0, pmCeil, gy + gh, gy)
    );
  }
  vertex(map(activeData.length - 1, 0, activeN, gx, gx + gw), gy + gh);
  endShape(CLOSE);

  // PM2.5 line
  stroke(...C.pm);
  strokeWeight(2);
  noFill();
  beginShape();
  for (let i = 0; i < activeData.length; i++) {
    vertex(
      map(i, 0, activeN, gx, gx + gw),
      map(activeData[i].pm2p5, 0, pmCeil, gy + gh, gy)
    );
  }
  endShape();
  noStroke();

  // Extra series — each normalized to its own range, thin lines, no fill
  const extraSeries = [
    { key: 'temp',     col: C.temp, label: 'Temp',     unit: '°C',    minFloor: 10,  decimals: 1 },
    { key: 'humidity', col: C.humi, label: 'Humidity', unit: '%',     minFloor: 50,  decimals: 1 },
    { key: 'voc',      col: C.voc,  label: 'VOC',      unit: 'idx',   minFloor: 100, decimals: 0 },
    { key: 'nox',      col: C.nox,  label: 'NOx',      unit: 'idx',   minFloor: 10,  decimals: 0 },
    { key: 'pm1',      col: C.pm1,  label: 'PM1.0',    unit: 'µg/m³', minFloor: 5,   decimals: 1 },
  ];
  for (let s of extraSeries) {
    let maxVal = Math.max(...activeData.map(d => d[s.key]));
    if (maxVal < s.minFloor) maxVal = s.minFloor;
    s.ceil = Math.ceil(maxVal * 1.2 / 5) * 5;
    stroke(...s.col);
    strokeWeight(1.5);
    noFill();
    beginShape();
    for (let i = 0; i < activeData.length; i++) {
      vertex(
        map(i, 0, activeN, gx, gx + gw),
        map(activeData[i][s.key], 0, s.ceil, gy + gh, gy)
      );
    }
    endShape();
  }
  noStroke();

  // Legend — all series in one horizontal row
  const allLegend = [
    { label: 'PM2.5',    col: C.pm   },
    { label: 'CO₂',      col: C.co2  },
    { label: 'Temp',     col: C.temp },
    { label: 'Humidity', col: C.humi },
    { label: 'VOC',      col: C.voc  },
    { label: 'NOx',      col: C.nox  },
    { label: 'PM1.0',    col: C.pm1  },
  ];
  noStroke();
  textSize(10);
  let lx = gx + 140;
  for (let item of allLegend) {
    fill(...item.col);
    rect(lx, y + 14, 14, 3, 2);
    fill(...C.text);
    textAlign(LEFT, CENTER);
    text(item.label, lx + 18, y + 16);
    lx += 18 + textWidth(item.label) + 14;
  }

  // X axis ticks — evenly spaced labels that don't overlap
  fill(...C.muted);
  textSize(9);
  const maxTicks   = floor(gw / (liveActive ? 80 : 40));           // ~80 px minimum gap
  const tickCount  = constrain(maxTicks, 2, 8);
  const drawableCount = round(gw / activeData.length);
  
  for (let t = 0; t < tickCount; t++) {
    let frac  = t / (tickCount - 1);
    let idx   = constrain(round(frac * activeN), 0, activeData.length - 1);
    let tx    = map(idx, 0, activeN, gx, gx + gw);
    let label = activeData[idx].time;

    // Tick mark
    stroke(...C.border);
    strokeWeight(1);
    line(tx, gy + gh, tx, gy + gh + 4);
    noStroke();

    // Label — left-align first, right-align last, centre the rest
    if (t === 0)             { textAlign(LEFT,   TOP); }
    else if (t === tickCount - 1) { textAlign(RIGHT,  TOP); }
    else                     { textAlign(CENTER, TOP); }

    if (tx <= activeData.length * (gw/activeN) && tx >= gx || tx >= gw) text(label, tx, gy + gh + 7);
  }

  // ── Hover tooltip ─────────────────────────────────────────────────────────
  if (mouseX >= gx && mouseX <= gx + gw && mouseY >= gy && mouseY <= gy + gh) {
    let idx = constrain(round(map(mouseX, gx, gx + gw, 0, activeN)), 0, activeData.length - 1);
    let hx  = map(idx, 0, activeN, gx, gx + gw);

    // Vertical snap line
    stroke(...C.border);
    strokeWeight(1);
    drawingContext.setLineDash([3, 5]);
    line(hx, gy, hx, gy + gh);
    drawingContext.setLineDash([]);
    noStroke();

    // Dots on main series
    let pmY  = map(activeData[idx].pm2p5, 0, pmCeil,  gy + gh, gy);
    let co2Y = map(activeData[idx].co2,   0, co2Ceil, gy + gh, gy);
    stroke(...C.bg);
    strokeWeight(2);
    fill(...C.pm);  circle(hx, pmY,  8);
    fill(...C.co2); circle(hx, co2Y, 8);

    // Dots on extra series
    for (let s of extraSeries) {
      let sy = map(activeData[idx][s.key], 0, s.ceil, gy + gh, gy);
      fill(...s.col); circle(hx, sy, 7);
    }
    noStroke();

    // Tooltip card
    const TW = 168, TH = 170, TR = 6, TP = 10;
    let tipX = hx + 12;
    let tipY = gy + 8;
    if (tipX + TW > gx + gw + pad.right - 4) tipX = hx - TW - 12;

    fill(19, 22, 34, 220);
    rect(tipX, tipY, TW, TH, TR);
    stroke(...C.border);
    strokeWeight(1);
    noFill();
    rect(tipX, tipY, TW, TH, TR);
    noStroke();

    let d = activeData[idx];
    fill(...C.muted);
    textSize(9);
    textAlign(LEFT, TOP);
    text(d.time, tipX + TP, tipY + TP);

    const tipRows = [
      { label: 'PM2.5',    col: C.pm,   val: fmt(d.pm2p5)   + ' µg/m³' },
      { label: 'CO₂',      col: C.co2,  val: fmt(d.co2, 0)  + ' ppm'   },
      { label: 'Temp',     col: C.temp, val: fmt(d.temp)     + ' °C'    },
      { label: 'Humidity', col: C.humi, val: fmt(d.humidity) + ' %'     },
      { label: 'VOC',      col: C.voc,  val: fmt(d.voc, 0)  + ' idx'   },
      { label: 'NOx',      col: C.nox,  val: fmt(d.nox, 0)  + ' idx'   },
      { label: 'PM1.0',    col: C.pm1,  val: fmt(d.pm1)      + ' µg/m³' },
    ];
    textSize(10);
    for (let i = 0; i < tipRows.length; i++) {
      let ry = tipY + 28 + i * 19;
      fill(...tipRows[i].col);
      textAlign(LEFT, TOP);
      text(tipRows[i].label, tipX + TP, ry);
      fill(...C.text);
      textAlign(RIGHT, TOP);
      text(tipRows[i].val, tipX + TW - TP, ry);
    }
  }
}

// ── Helpers ──────────────────────────────────────────────────────────────────
function fmt(val, decimals = 1) {
  if (val === undefined || val === null) return "--";
  return Number(val).toFixed(decimals);
}

function pmAQI(pm) {
  if (pm == null) return null;
  if (pm < 12)    return { label: "Good",      color: C.ok     };
  if (pm < 35)    return { label: "Moderate",  color: C.warn   };
  return                 { label: "Unhealthy", color: C.danger };
}

function co2Level(co2) {
  if (co2 == null) return null;
  if (co2 < 800)   return { label: "Good",     color: C.ok     };
  if (co2 < 1500)  return { label: "Moderate", color: C.warn   };
  return                  { label: "High",      color: C.danger };
}

function windowResized() {
  resizeCanvas(windowWidth, windowHeight);
}

function mousePressed() {
  if (_btnLiveRect &&
      mouseX >= _btnLiveRect.x && mouseX <= _btnLiveRect.x + _btnLiveRect.w &&
      mouseY >= _btnLiveRect.y && mouseY <= _btnLiveRect.y + _btnLiveRect.h) {
    historyMode = "live";
    return false;
  }
  if (_btnApiRect &&
      mouseX >= _btnApiRect.x && mouseX <= _btnApiRect.x + _btnApiRect.w &&
      mouseY >= _btnApiRect.y && mouseY <= _btnApiRect.y + _btnApiRect.h) {
    historyMode = "api";
    fetchApiHistory();
    return false;
  }
}

function drawLogo(logoScale = 0.3) {
    push()
        scale(logoScale / 9, logoScale / 9);
        fill("rgba(0, 0, 0, 0)")
        stroke('rgba(0,0,0,0)')
        strokeCap(PROJECT);
        strokeJoin(MITER);
        fill("#ff5c00")
        beginShape();
        vertex(0,362.58);
        bezierVertex(0,385.331); bezierVertex(7.85118,404.156); bezierVertex(23.9996,420.393);
        bezierVertex(40.148,436.542); bezierVertex(59.0623,444.393); bezierVertex(81.902,444.393);
        bezierVertex(82.2589,444.393); bezierVertex(82.6157,444.393); bezierVertex(82.9726,444.393);
        bezierVertex(97.5151,444.214); bezierVertex(111.522,449.478); bezierVertex(121.782,459.738);
        vertex(125.262,463.218);
        bezierVertex(135.7,473.656); bezierVertex(140.607,487.931); bezierVertex(140.607,502.652);
        bezierVertex(140.607,502.652); bezierVertex(140.607,502.83); bezierVertex(140.607,502.92);
        bezierVertex(140.607,525.67); bezierVertex(148.458,544.584); bezierVertex(164.607,560.733);
        bezierVertex(180.755,576.881); bezierVertex(199.669,584.732); bezierVertex(222.509,584.732);
        bezierVertex(245.259,584.732); bezierVertex(264.174,576.881); bezierVertex(280.322,560.733);
        bezierVertex(296.47,544.584); bezierVertex(304.322,525.581); bezierVertex(304.322,502.92);
        bezierVertex(304.322,480.169); bezierVertex(296.47,461.255); bezierVertex(280.322,445.107);
        bezierVertex(264.174,428.958); bezierVertex(245.259,421.107); bezierVertex(222.509,421.107);
        bezierVertex(222.152,421.107); bezierVertex(221.795,421.107); bezierVertex(221.438,421.107);
        bezierVertex(206.896,421.285); bezierVertex(192.889,416.022); bezierVertex(182.54,405.761);
        vertex(180.666,403.888);
        bezierVertex(169.781,393.003); bezierVertex(163.715,378.282); bezierVertex(163.715,362.848);
        vertex(163.715,362.58);
        bezierVertex(163.715,339.83); bezierVertex(155.863,320.916); bezierVertex(139.715,304.767);
        bezierVertex(123.567,288.619); bezierVertex(104.653,280.768); bezierVertex(81.902,280.768);
        bezierVertex(59.1515,280.768); bezierVertex(40.2373,288.619); bezierVertex(24.0889,304.767);
        bezierVertex(7.94048,320.916); bezierVertex(0,340.9); bezierVertex(0,362.759);
        vertex(0,362.58);
        endShape();
        fill("#ff5c00")
        beginShape();
        vertex(223.309,584.91);
        bezierVertex(246.059,584.91); bezierVertex(264.884,577.058); bezierVertex(281.122,560.91);
        bezierVertex(297.27,544.762); bezierVertex(305.121,525.847); bezierVertex(305.121,503.008);
        bezierVertex(305.121,502.651); bezierVertex(305.121,502.294); bezierVertex(305.121,501.937);
        bezierVertex(304.943,487.395); bezierVertex(310.207,473.387); bezierVertex(320.467,463.127);
        vertex(323.946,459.648);
        bezierVertex(334.385,449.21); bezierVertex(348.659,444.303); bezierVertex(363.38,444.303);
        bezierVertex(363.38,444.303); bezierVertex(363.559,444.303); bezierVertex(363.648,444.303);
        bezierVertex(386.399,444.303); bezierVertex(405.313,436.451); bezierVertex(421.461,420.303);
        bezierVertex(437.61,404.155); bezierVertex(445.461,385.24); bezierVertex(445.461,362.401);
        bezierVertex(445.461,339.65); bezierVertex(437.61,320.736); bezierVertex(421.461,304.588);
        bezierVertex(405.313,288.439); bezierVertex(386.309,280.588); bezierVertex(363.648,280.588);
        bezierVertex(340.898,280.588); bezierVertex(321.984,288.439); bezierVertex(305.835,304.588);
        bezierVertex(289.687,320.736); bezierVertex(281.836,339.65); bezierVertex(281.836,362.401);
        bezierVertex(281.836,362.757); bezierVertex(281.836,363.114); bezierVertex(281.836,363.471);
        bezierVertex(282.014,378.014); bezierVertex(276.75,392.021); bezierVertex(266.49,402.37);
        vertex(264.616,404.244);
        bezierVertex(253.732,415.128); bezierVertex(239.011,421.195); bezierVertex(223.576,421.195);
        vertex(223.309,421.195);
        bezierVertex(200.558,421.195); bezierVertex(181.644,429.046); bezierVertex(165.496,445.195);
        bezierVertex(149.347,461.343); bezierVertex(141.496,480.257); bezierVertex(141.496,503.008);
        bezierVertex(141.496,525.758); bezierVertex(149.347,544.672); bezierVertex(165.496,560.821);
        bezierVertex(181.644,576.969); bezierVertex(201.629,584.91); bezierVertex(223.487,584.91);
        vertex(223.309,584.91);
        endShape();
        fill("#ff5c00")
        beginShape();
        vertex(281.984,362.571);
        bezierVertex(281.984,385.322); bezierVertex(289.836,404.146); bezierVertex(305.984,420.384);
        bezierVertex(322.132,436.532); bezierVertex(341.047,444.384); bezierVertex(363.886,444.384);
        bezierVertex(364.243,444.384); bezierVertex(364.6,444.384); bezierVertex(364.957,444.384);
        bezierVertex(379.499,444.205); bezierVertex(393.506,449.469); bezierVertex(403.766,459.729);
        vertex(407.246,463.209);
        bezierVertex(417.685,473.647); bezierVertex(422.592,487.922); bezierVertex(422.592,502.643);
        bezierVertex(422.592,502.643); bezierVertex(422.592,502.821); bezierVertex(422.592,502.91);
        bezierVertex(422.592,525.661); bezierVertex(430.443,544.575); bezierVertex(446.591,560.724);
        bezierVertex(462.74,576.872); bezierVertex(481.654,584.723); bezierVertex(504.493,584.723);
        bezierVertex(527.244,584.723); bezierVertex(546.158,576.872); bezierVertex(562.306,560.724);
        bezierVertex(578.455,544.575); bezierVertex(586.306,525.572); bezierVertex(586.306,502.91);
        bezierVertex(586.306,480.16); bezierVertex(578.455,461.246); bezierVertex(562.306,445.097);
        bezierVertex(546.158,428.949); bezierVertex(527.244,421.098); bezierVertex(504.493,421.098);
        bezierVertex(504.136,421.098); bezierVertex(503.78,421.098); bezierVertex(503.423,421.098);
        bezierVertex(488.88,421.276); bezierVertex(474.873,416.012); bezierVertex(464.524,405.752);
        vertex(462.65,403.879);
        bezierVertex(451.766,392.994); bezierVertex(445.699,378.273); bezierVertex(445.699,362.839);
        vertex(445.699,362.571);
        bezierVertex(445.699,339.821); bezierVertex(437.848,320.906); bezierVertex(421.699,304.758);
        bezierVertex(405.551,288.61); bezierVertex(386.637,280.758); bezierVertex(363.886,280.758);
        bezierVertex(341.136,280.758); bezierVertex(322.222,288.61); bezierVertex(306.073,304.758);
        bezierVertex(289.925,320.906); bezierVertex(281.984,340.891); bezierVertex(281.984,362.749);
        vertex(281.984,362.571);
        endShape();
        fill("#ff5c00")
        beginShape();
        vertex(505.382,584.9);
        bezierVertex(528.133,584.9); bezierVertex(546.958,577.049); bezierVertex(563.195,560.901);
        bezierVertex(579.344,544.752); bezierVertex(587.195,525.838); bezierVertex(587.195,502.998);
        bezierVertex(587.195,502.642); bezierVertex(587.195,502.285); bezierVertex(587.195,501.928);
        bezierVertex(587.016,487.385); bezierVertex(592.28,473.378); bezierVertex(602.54,463.118);
        vertex(606.02,459.639);
        bezierVertex(616.458,449.2); bezierVertex(630.733,444.293); bezierVertex(645.454,444.293);
        bezierVertex(645.454,444.293); bezierVertex(645.632,444.293); bezierVertex(645.722,444.293);
        bezierVertex(668.472,444.293); bezierVertex(687.386,436.442); bezierVertex(703.534,420.294);
        bezierVertex(719.683,404.145); bezierVertex(727.534,385.231); bezierVertex(727.534,362.391);
        bezierVertex(727.534,339.641); bezierVertex(719.683,320.727); bezierVertex(703.534,304.578);
        bezierVertex(687.386,288.43); bezierVertex(668.383,280.579); bezierVertex(645.722,280.579);
        bezierVertex(622.971,280.579); bezierVertex(604.057,288.43); bezierVertex(587.908,304.578);
        bezierVertex(571.76,320.727); bezierVertex(563.909,339.641); bezierVertex(563.909,362.391);
        bezierVertex(563.909,362.748); bezierVertex(563.909,363.105); bezierVertex(563.909,363.462);
        bezierVertex(564.087,378.005); bezierVertex(558.823,392.012); bezierVertex(548.563,402.361);
        vertex(546.69,404.234);
        bezierVertex(535.805,415.119); bezierVertex(521.084,421.186); bezierVertex(505.65,421.186);
        vertex(505.382,421.186);
        bezierVertex(482.632,421.186); bezierVertex(463.717,429.037); bezierVertex(447.569,445.185);
        bezierVertex(431.421,461.334); bezierVertex(423.569,480.248); bezierVertex(423.569,502.998);
        bezierVertex(423.569,525.749); bezierVertex(431.421,544.663); bezierVertex(447.569,560.811);
        bezierVertex(463.717,576.96); bezierVertex(483.702,584.9); bezierVertex(505.56,584.9);
        vertex(505.382,584.9);
        endShape();
        fill("#ff5c00")
        beginShape();
        vertex(282.34,82.3387);
        bezierVertex(282.34,105.089); bezierVertex(290.191,123.914); bezierVertex(306.339,140.152);
        bezierVertex(322.488,156.3); bezierVertex(341.402,164.151); bezierVertex(364.242,164.151);
        bezierVertex(364.598,164.151); bezierVertex(364.955,164.151); bezierVertex(365.312,164.151);
        bezierVertex(379.855,163.973); bezierVertex(393.862,169.237); bezierVertex(404.122,179.497);
        vertex(407.602,182.976);
        bezierVertex(418.04,193.415); bezierVertex(422.947,207.69); bezierVertex(422.947,222.41);
        bezierVertex(422.947,222.41); bezierVertex(422.947,222.589); bezierVertex(422.947,222.678);
        bezierVertex(422.947,245.429); bezierVertex(430.798,264.343); bezierVertex(446.947,280.491);
        bezierVertex(463.095,296.639); bezierVertex(482.009,304.491); bezierVertex(504.849,304.491);
        bezierVertex(527.599,304.491); bezierVertex(546.513,296.639); bezierVertex(562.662,280.491);
        bezierVertex(578.81,264.343); bezierVertex(586.661,245.339); bezierVertex(586.661,222.678);
        bezierVertex(586.661,199.928); bezierVertex(578.81,181.013); bezierVertex(562.662,164.865);
        bezierVertex(546.513,148.717); bezierVertex(527.599,140.865); bezierVertex(504.849,140.865);
        bezierVertex(504.492,140.865); bezierVertex(504.135,140.865); bezierVertex(503.778,140.865);
        bezierVertex(489.236,141.044); bezierVertex(475.228,135.78); bezierVertex(464.879,125.52);
        vertex(463.006,123.646);
        bezierVertex(452.121,112.762); bezierVertex(446.054,98.041); bezierVertex(446.054,82.6063);
        vertex(446.054,82.3387);
        bezierVertex(446.054,59.5882); bezierVertex(438.203,40.674); bezierVertex(422.055,24.5256);
        bezierVertex(405.906,8.37718); bezierVertex(386.992,0.526001); bezierVertex(364.242,0.526001);
        bezierVertex(341.491,0.526001); bezierVertex(322.577,8.37718); bezierVertex(306.429,24.5256);
        bezierVertex(290.28,40.674); bezierVertex(282.34,60.6587); bezierVertex(282.34,82.5171);
        vertex(282.34,82.3387);
        endShape();
        fill("#ff5c00")
        beginShape();
        vertex(223.131,304.322);
        bezierVertex(245.881,304.322); bezierVertex(264.706,296.47); bezierVertex(280.944,280.322);
        bezierVertex(297.092,264.174); bezierVertex(304.944,245.259); bezierVertex(304.944,222.42);
        bezierVertex(304.944,222.063); bezierVertex(304.944,221.706); bezierVertex(304.944,221.349);
        bezierVertex(304.765,206.807); bezierVertex(310.029,192.799); bezierVertex(320.289,182.539);
        vertex(323.768,179.06);
        bezierVertex(334.207,168.621); bezierVertex(348.482,163.714); bezierVertex(363.203,163.714);
        bezierVertex(363.203,163.714); bezierVertex(363.381,163.714); bezierVertex(363.47,163.714);
        bezierVertex(386.221,163.714); bezierVertex(405.135,155.863); bezierVertex(421.283,139.715);
        bezierVertex(437.432,123.567); bezierVertex(445.283,104.652); bezierVertex(445.283,81.8126);
        bezierVertex(445.283,59.0621); bezierVertex(437.432,40.148); bezierVertex(421.283,23.9996);
        bezierVertex(405.135,7.85118); bezierVertex(386.132,0); bezierVertex(363.47,0);
        bezierVertex(340.72,0); bezierVertex(321.806,7.85118); bezierVertex(305.657,23.9996);
        bezierVertex(289.509,40.148); bezierVertex(281.658,59.0621); bezierVertex(281.658,81.8126);
        bezierVertex(281.658,82.1695); bezierVertex(281.658,82.5264); bezierVertex(281.658,82.8832);
        bezierVertex(281.836,97.4257); bezierVertex(276.572,111.433); bezierVertex(266.312,121.782);
        vertex(264.439,123.656);
        bezierVertex(253.554,134.54); bezierVertex(238.833,140.607); bezierVertex(223.398,140.607);
        vertex(223.131,140.607);
        bezierVertex(200.38,140.607); bezierVertex(181.466,148.458); bezierVertex(165.318,164.607);
        bezierVertex(149.17,180.755); bezierVertex(141.318,199.669); bezierVertex(141.318,222.42);
        bezierVertex(141.318,245.17); bezierVertex(149.17,264.084); bezierVertex(165.318,280.233);
        bezierVertex(181.466,296.381); bezierVertex(201.451,304.322); bezierVertex(223.309,304.322);
        vertex(223.131,304.322);
        endShape();
    pop()
}