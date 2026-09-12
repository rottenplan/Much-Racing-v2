#ifndef WEB_STATIC_H
#define WEB_STATIC_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>MuchRacing GPS - Data Analysis</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: #0a0a0a; color: #eee; font-family: 'Segoe UI', sans-serif; min-height: 100vh; display: flex; flex-direction: column; }
    .header { background: linear-gradient(135deg, #111 0%, #1a1a2e 100%); padding: 12px 20px; display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #04DF00; }
    .header h1 { font-size: 18px; color: #04DF00; letter-spacing: 1px; }
    .header .status { font-size: 12px; padding: 4px 10px; border-radius: 12px; background: #222; }
    .nav { background: #111; padding: 8px 10px; display: flex; gap: 6px; border-bottom: 1px solid #222; flex-wrap: wrap; justify-content: center; }
    .nav a { color: #888; text-decoration: none; font-size: 12px; padding: 6px 10px; border-radius: 6px; transition: all 0.2s; white-space: nowrap; }
    .nav a:hover, .nav a.active { background: #04DF00; color: #000; font-weight: bold; }
    .grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; padding: 16px 20px; flex-grow: 1; align-content: start; }
    @media (max-width: 600px) { .grid { grid-template-columns: repeat(2, 1fr); } }
    .card { background: linear-gradient(145deg, #1a1a1a 0%, #222 100%); border-radius: 12px; padding: 16px; text-align: center; border: 1px solid #333; transition: border-color 0.3s; }
    .card:hover { border-color: #04DF00; }
    .card.speed { grid-column: span 3; background: linear-gradient(145deg, #0d1117 0%, #161b22 100%); border-color: #04DF00; }
    @media (max-width: 600px) { .card.speed { grid-column: span 2; } }
    .card.wide { grid-column: span 3; }
    @media (max-width: 600px) { .card.wide { grid-column: span 2; } }
    .label { font-size: 11px; color: #888; text-transform: uppercase; letter-spacing: 2px; margin-bottom: 6px; }
    .value { font-size: 22px; font-weight: bold; color: #fff; }
    .value.speed-val { font-size: 64px; color: #04DF00; font-family: 'Courier New', monospace; }
    .unit { font-size: 11px; color: #04DF00; margin-left: 2px; }
    .coord { font-size: 13px; color: #aaa; font-family: monospace; }
    .footer { background: #111; padding: 10px 20px; text-align: center; font-size: 11px; color: #444; border-top: 1px solid #222; }
  </style>
</head>
<body>
  <div class="header">
    <h1>MUCH RACING</h1>
    <div class="status" id="connection-status" style="color:yellow;">Connecting...</div>
  </div>
  <div class="nav">
    <a href="/" class="active">Dashboard</a>
    <a href="/rpm">RPM Sensor</a>
    <a href="/sessions">Sessions</a>
    <a href="/update">Firmware</a>
  </div>

  <div class="grid">
    <div class="card speed">
      <div class="label">Speed</div>
      <div><span id="speed" class="value speed-val">0</span> <span class="unit" style="font-size:18px;">KM/H</span></div>
    </div>

    <div class="card">
      <div class="label">RPM</div>
      <div><span id="rpm" class="value">0</span> <span class="unit">RPM</span></div>
    </div>
    <div class="card">
      <div class="label">Trip</div>
      <div><span id="trip" class="value">0.0</span> <span class="unit">KM</span></div>
    </div>
    <div class="card">
      <div class="label">Satellites</div>
      <div><span id="sats" class="value">0</span> <span class="unit">SAT</span></div>
    </div>

    <div class="card">
      <div class="label">Battery</div>
      <div><span id="bat" class="value">0</span> <span class="unit">%</span></div>
    </div>
    <div class="card">
      <div class="label">Voltage</div>
      <div><span id="volt" class="value">0.0</span> <span class="unit">V</span></div>
    </div>
    <div class="card">
      <div class="label">Charging</div>
      <div><span id="chg" class="value">-</span></div>
    </div>

    <div class="card wide">
      <div class="label">GPS Coordinates</div>
      <div class="coord"><span id="lat">0.000000</span>, <span id="lng">0.000000</span></div>
    </div>
  </div>

  <div class="footer">MuchRacing GPS &bull; Offline Data Analysis</div>

  <script>
    function updateStats() {
      fetch('/api/live')
        .then(r => { if (!r.ok) throw new Error("HTTP " + r.status); return r.json(); })
        .then(d => {
          document.getElementById('connection-status').innerText = "Live";
          document.getElementById('connection-status').style.color = "#04DF00";
          document.getElementById('speed').innerText = Math.round(d.speed);
          document.getElementById('rpm').innerText = d.rpm;
          document.getElementById('trip').innerText = d.trip.toFixed(1);
          document.getElementById('sats').innerText = d.sats;
          document.getElementById('bat').innerText = d.bat_percent;
          document.getElementById('volt').innerText = d.bat_voltage.toFixed(2);
          document.getElementById('chg').innerText = d.is_charging ? "Yes" : "No";
          document.getElementById('chg').style.color = d.is_charging ? "#04DF00" : "#888";
          document.getElementById('lat').innerText = d.lat.toFixed(6);
          document.getElementById('lng').innerText = d.lng.toFixed(6);
        })
        .catch(e => {
          document.getElementById('connection-status').innerText = "Err: " + e.message;
          document.getElementById('connection-status').style.color = "red";
        });
    }
    setInterval(updateStats, 500);
  </script>
</body>
</html>
)rawliteral";

const char RPM_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>RPM Sensor - MuchRacing</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: #0a0a0a; color: #eee; font-family: 'Segoe UI', sans-serif; min-height: 100vh; display: flex; flex-direction: column; }
    .header { background: linear-gradient(135deg, #111 0%, #1a1a2e 100%); padding: 12px 20px; display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #04DF00; }
    .header h1 { font-size: 18px; color: #04DF00; letter-spacing: 1px; }
    .header .status { font-size: 12px; padding: 4px 10px; border-radius: 12px; background: #222; }
    .nav { background: #111; padding: 8px 10px; display: flex; gap: 6px; border-bottom: 1px solid #222; flex-wrap: wrap; justify-content: center; }
    .nav a { color: #888; text-decoration: none; font-size: 12px; padding: 6px 10px; border-radius: 6px; transition: all 0.2s; white-space: nowrap; }
    .nav a:hover, .nav a.active { background: #04DF00; color: #000; font-weight: bold; }
    .content { padding: 20px; flex-grow: 1; display: flex; flex-direction: column; gap: 16px; align-items: center; }
    .rpm-display { background: linear-gradient(145deg, #0d1117 0%, #161b22 100%); border: 2px solid #04DF00; border-radius: 16px; padding: 20px; text-align: center; width: 100%; max-width: 400px; }
    .rpm-value { font-size: 64px; color: #04DF00; font-family: 'Courier New', monospace; font-weight: bold; line-height: 1; margin: 10px 0; }
    .rpm-label { font-size: 14px; color: #888; text-transform: uppercase; letter-spacing: 3px; }
    .rpm-bar { width: 100%; height: 16px; background: #222; border-radius: 8px; overflow: hidden; margin-top: 10px; }
    .rpm-bar-fill { height: 100%; background: linear-gradient(90deg, #04DF00, #FFD700, #FF4444); width: 0%; transition: width 0.2s; border-radius: 8px; }
    
    .graph-container { background: #111; border: 1px solid #333; border-radius: 12px; padding: 12px; width: 100%; max-width: 400px; }
    .graph-title { font-size: 11px; color: #888; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 10px; text-align: left; display: flex; justify-content: space-between; }
    canvas { width: 100%; height: 120px; background: #0a0a0a; border-radius: 6px; border: 1px solid #222; display: block; }
    
    .info-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; width: 100%; max-width: 400px; }
    .info-card { background: linear-gradient(145deg, #1a1a1a, #222); border-radius: 10px; padding: 14px; text-align: center; border: 1px solid #333; }
    .info-label { font-size: 11px; color: #888; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 4px; }
    .info-val { font-size: 18px; font-weight: bold; color: #fff; }
    .footer { background: #111; padding: 10px 20px; text-align: center; font-size: 11px; color: #444; border-top: 1px solid #222; margin-top: auto; }
  </style>
</head>
<body>
  <div class="header">
    <h1>MUCH RACING</h1>
    <div class="status" id="conn" style="color:yellow;">Connecting...</div>
  </div>
  <div class="nav">
    <a href="/">Dashboard</a>
    <a href="/rpm" class="active">RPM Sensor</a>
    <a href="/sessions">Sessions</a>
    <a href="/update">Firmware</a>
  </div>

  <div class="content">
    <div class="rpm-display">
      <div class="rpm-label">Engine RPM</div>
      <div class="rpm-value" id="rpm">0</div>
      <div class="rpm-bar"><div class="rpm-bar-fill" id="rpmBar"></div></div>
    </div>
    
    <div class="graph-container">
      <div class="graph-title">
        <span>Live Telemetry</span>
        <span>
          <span style="color:#FF4444; font-weight:bold;">&bull; RPM</span> 
        </span>
      </div>
      <canvas id="graph"></canvas>
    </div>

    <div class="info-grid">
      <div class="info-card">
        <div class="info-label">RPM Live</div>
        <div class="info-val"><span id="liveRpm">0</span></div>
      </div>
      <div class="info-card">
        <div class="info-label">Gear</div>
        <div class="info-val" id="gear" style="color:#04DF00;">N</div>
      </div>
      <div class="info-card">
        <div class="info-label">Battery</div>
        <div class="info-val"><span id="bat">0</span><span style="color:#04DF00;font-size:11px;">%</span></div>
      </div>
      <div class="info-card">
        <div class="info-label">Peak RPM</div>
        <div class="info-val"><span id="peakRpm">0</span></div>
      </div>
    </div>
  </div>
  <div class="footer">RPM Sensor Monitor &bull; MuchRacing GPS</div>
  
  <script>
    const MAX_RPM = 14000;
    const histSize = 50;
    let rpmHist = new Array(histSize).fill(0);
    const canvas = document.getElementById('graph');
    const ctx = canvas.getContext('2d');
    let maxRpmVal = 0;

    function drawGraph() {
      if(!canvas) return;
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      canvas.width = rect.width * dpr;
      canvas.height = rect.height * dpr;
      const w = canvas.width;
      const h = canvas.height;
      
      ctx.fillStyle = '#0a0a0a';
      ctx.fillRect(0, 0, w, h);
      
      ctx.strokeStyle = '#222';
      ctx.lineWidth = 1 * dpr;
      for(let i=1; i<4; i++) {
        let y = (h*i)/4;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
      }
      for(let i=1; i<6; i++) {
        let x = (w*i)/6;
        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
      }
      
      let step = w / (histSize - 1);
      
      function traceLine(data, color, maxVal) {
        ctx.beginPath();
        ctx.strokeStyle = color;
        ctx.lineWidth = 2 * dpr;
        ctx.lineJoin = 'round';
        for(let i=0; i<histSize; i++) {
          let x = i * step;
          let y = h - Math.max(0, Math.min((data[i]/maxVal)*h, h));
          if(i===0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.stroke();
      }
      
      traceLine(rpmHist, '#FF4444', MAX_RPM);
    }
    
    setTimeout(drawGraph, 100);

    function update() {
      fetch('/api/live')
        .then(r => r.json())
        .then(d => {
          document.getElementById('conn').innerText = 'Live';
          document.getElementById('conn').style.color = '#04DF00';
          document.getElementById('rpm').innerText = d.rpm;
          document.getElementById('liveRpm').innerText = d.rpm;
          document.getElementById('bat').innerText = d.bat_percent;
          
          if(d.rpm > maxRpmVal) maxRpmVal = d.rpm;
          document.getElementById('peakRpm').innerText = maxRpmVal;
          
          let pct = Math.min((d.rpm / MAX_RPM) * 100, 100);
          document.getElementById('rpmBar').style.width = pct + '%';
          
          // Gear Calculation Heuristic
          let gearStr = "N";
          if (d.speed > 3 && d.rpm > 800) {
            let ratio = d.rpm / d.speed;
            if (ratio > 110) gearStr = "1";
            else if (ratio > 80) gearStr = "2";
            else if (ratio > 60) gearStr = "3";
            else if (ratio > 45) gearStr = "4";
            else if (ratio > 35) gearStr = "5";
            else gearStr = "6";
          }
          let gEl = document.getElementById('gear');
          gEl.innerText = gearStr;
          gEl.style.color = (gearStr === 'N') ? '#888' : '#04DF00';

          rpmHist.push(d.rpm);
          rpmHist.shift();
          drawGraph();
        })
        .catch(e => {
          document.getElementById('conn').innerText = 'Error';
          document.getElementById('conn').style.color = 'red';
        });
    }
    // Update more frequently for smoother graph (200ms = 5Hz)
    setInterval(update, 200);
    // Resize handler
    window.addEventListener('resize', drawGraph);
  </script>
</body>
</html>
)rawliteral";

const char UPDATE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>Firmware Update - MuchRacing</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: #0a0a0a; color: #eee; font-family: 'Segoe UI', sans-serif; min-height: 100vh; display: flex; flex-direction: column; }
    .header { background: linear-gradient(135deg, #111 0%, #1a1a2e 100%); padding: 12px 20px; display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #04DF00; }
    .header h1 { font-size: 18px; color: #04DF00; letter-spacing: 1px; }
    .nav { background: #111; padding: 8px 10px; display: flex; gap: 6px; border-bottom: 1px solid #222; flex-wrap: wrap; justify-content: center; }
    .nav a { color: #888; text-decoration: none; font-size: 12px; padding: 6px 10px; border-radius: 6px; transition: all 0.2s; white-space: nowrap; }
    .nav a:hover, .nav a.active { background: #04DF00; color: #000; font-weight: bold; }
    
    .content { padding: 30px 20px; flex-grow: 1; display: flex; flex-direction: column; align-items: center; }
    .container { background: linear-gradient(145deg, #1a1a1a, #222); padding: 30px; border-radius: 12px; border: 1px solid #04DF00; width: 100%; max-width: 400px; text-align: center; }
    h2 { margin-bottom: 12px; }
    input[type=file] { background: #333; padding: 10px; border-radius: 5px; width: 100%; margin-bottom: 20px; color: #fff; }
    button { background: #04DF00; color: #000; border: none; padding: 12px 20px; border-radius: 5px; font-weight: bold; cursor: pointer; width: 100%; font-size: 14px; }
    #prg { margin-top: 20px; background: #333; height: 20px; border-radius: 10px; overflow: hidden; display: none; }
    #bar { background: #04DF00; height: 100%; width: 0%; transition: width 0.3s; }
    #status { margin-top: 15px; font-size: 14px; font-weight: bold; }
    .footer { background: #111; padding: 10px 20px; text-align: center; font-size: 11px; color: #444; border-top: 1px solid #222; margin-top: auto; }
  </style>
</head>
<body>
  <div class="header">
    <h1>MUCH RACING</h1>
  </div>
  <div class="nav">
    <a href="/">Dashboard</a>
    <a href="/rpm">RPM Sensor</a>
    <a href="/sessions">Sessions</a>
    <a href="/update" class="active">Firmware</a>
  </div>

  <div class="content">
    <div class="container">
      <h2>MuchUpdate</h2>
      <p style="color:#aaa; font-size:12px; margin-bottom:20px;">Select firmware file (.bin)</p>
      <form id="upload_form" enctype="multipart/form-data" method="post">
        <input type="file" name="update" accept=".bin">
        <button type="submit">Upload & Update</button>
      </form>
      <div id="prg"><div id="bar"></div></div>
      <p id="status"></p>
    </div>
  </div>
  
  <div class="footer">MuchRacing GPS &bull; Firmware OTA</div>

  <script>
    var form = document.getElementById('upload_form');
    form.addEventListener('submit', e => {
      e.preventDefault();
      var data = new FormData(form);
      var req = new XMLHttpRequest();
      req.open('POST', '/update');
      
      document.getElementById('prg').style.display = 'block';
      document.getElementById('status').innerText = "Uploading...";
      document.getElementById('status').style.color = "#04DF00";
      
      req.upload.addEventListener('progress', p => {
        var pc = (p.loaded / p.total) * 100;
        document.getElementById('bar').style.width = pc + '%';
      });
      
      req.onload = () => {
        if (req.status == 200) {
          document.getElementById('status').innerText = "Success! Rebooting...";
          setTimeout(() => { window.location.href = '/'; }, 5000);
        } else {
          document.getElementById('status').innerText = "Error: " + req.responseText;
          document.getElementById('status').style.color = "red";
        }
      };
      req.send(data);
    });
  </script>
</body>
</html>
)rawliteral";

const char SESSIONS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>Session Manager - MuchRacing</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: #0a0a0a; color: #eee; font-family: 'Segoe UI', sans-serif; min-height: 100vh; display: flex; flex-direction: column; }
    .header { background: linear-gradient(135deg, #111 0%, #1a1a2e 100%); padding: 12px 20px; display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #04DF00; }
    .header h1 { font-size: 18px; color: #04DF00; letter-spacing: 1px; }
    .nav { background: #111; padding: 8px 10px; display: flex; gap: 6px; border-bottom: 1px solid #222; flex-wrap: wrap; justify-content: center; }
    .nav a { color: #888; text-decoration: none; font-size: 12px; padding: 6px 10px; border-radius: 6px; transition: all 0.2s; white-space: nowrap; }
    .nav a:hover, .nav a.active { background: #04DF00; color: #000; font-weight: bold; }
    
    .content { padding: 20px; flex-grow: 1; display: flex; flex-direction: column; align-items: center; }
    h2 { color: #fff; margin-bottom: 16px; font-size: 18px; }
    ul { list-style: none; width: 100%; max-width: 400px; }
    li { background: linear-gradient(145deg, #1a1a1a, #222); margin-bottom: 12px; padding: 15px; border-radius: 8px; display: flex; justify-content: space-between; align-items: center; border: 1px solid #333; }
    a.btn { background: #04DF00; color: #000; text-decoration: none; padding: 8px 15px; border-radius: 5px; font-weight: bold; font-size: 12px; }
    .name { font-size: 14px; font-weight: bold; color: #fff; margin-bottom: 4px; }
    .size { font-size: 11px; color: #04DF00; }
    .empty { color: #888; margin-top: 30px; font-style: italic; }
    
    .footer { background: #111; padding: 10px 20px; text-align: center; font-size: 11px; color: #444; border-top: 1px solid #222; margin-top: auto; }
  </style>
</head>
<body>
  <div class="header">
    <h1>MUCH RACING</h1>
  </div>
  <div class="nav">
    <a href="/">Dashboard</a>
    <a href="/rpm">RPM Sensor</a>
    <a href="/sessions" class="active">Sessions</a>
    <a href="/update">Firmware</a>
  </div>

  <div class="content">
    <h2>Data Sessions</h2>
    <ul id="list">
      <li style="justify-content:center;"><p class="empty" style="margin:0;">Loading files...</p></li>
    </ul>
  </div>
  
  <div class="footer">MuchRacing GPS &bull; GPX & CSV Logs</div>

  <script>
    fetch('/api/sessions')
      .then(res => res.json())
      .then(files => {
        const list = document.getElementById('list');
        list.innerHTML = '';
        if (files.length === 0) {
          list.innerHTML = '<li style="justify-content:center;"><p class="empty" style="margin:0;">No sessions found on SD Card.</p></li>';
          return;
        }
        files.forEach(f => {
          const li = document.createElement('li');
          li.innerHTML = `
            <div style="text-align:left;">
              <div class="name">${f.name}</div>
              <div class="size">${f.size}</div>
            </div>
            <a href="/download?file=${f.path}" class="btn">Download</a>
          `;
          list.appendChild(li);
        });
      })
      .catch(e => {
        document.getElementById('list').innerHTML = '<li style="justify-content:center;"><p class="empty" style="color:red; margin:0;">Error loading list.</p></li>';
      });
  </script>
</body>
</html>
)rawliteral";

#endif
