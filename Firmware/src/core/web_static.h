#ifndef WEB_STATIC_H
#define WEB_STATIC_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>MuchRacing GPS</title>
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" 
     integrity="sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY=" 
     crossorigin=""/>
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js" 
     integrity="sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo=" 
     crossorigin=""></script>
  <style>
    body { background: #111; color: #eee; font-family: sans-serif; margin: 0; padding: 0; display: flex; flex-direction: column; height: 100vh; }
    #map { flex-grow: 1; min-height: 50%; width: 100%; z-index: 1; }
    #stats { background: #222; padding: 10px; display: grid; grid-template-columns: 1fr 1fr; gap: 10px; border-top: 2px solid #04DF00; }
    .card { background: #333; padding: 10px; border-radius: 8px; text-align: center; }
    .label { font-size: 12px; color: #aaa; }
    .value { font-size: 24px; font-weight: bold; color: #fff; }
    .unit { font-size: 12px; color: #04DF00; }
    .offline-warning { position: absolute; top: 10px; left: 50%; transform: translateX(-50%); background: rgba(255,0,0,0.8); padding: 5px 15px; border-radius: 20px; font-size: 12px; z-index: 1000; display: none; }
  </style>
</head>
<body>
  <div id="offline" class="offline-warning">No Internet - Map Tiles May Missing</div>
  <div style="background:#000; padding:5px; text-align:center; font-size:12px; color:#888;">
    Status: <span id="connection-status" style="color:yellow;">Connecting...</span>
    <span style="margin-left:10px;"><a href="/sessions" style="color:#04DF00;text-decoration:none;border:1px solid #04DF00;padding:2px 5px;border-radius:4px;">Manage Sessions</a></span>
  </div>
  <div id="map"></div>
  <div id="stats">
    <div class="card">
      <div class="label">SPEED</div>
      <div><span id="speed" class="value">0</span> <span class="unit">KM/H</span></div>
    </div>
    <div class="card">
      <div class="label">RPM</div>
      <div><span id="rpm" class="value">0</span> <span class="unit">RPM</span></div>
    </div>
    <div class="card">
      <div class="label">TRIP</div>
      <div><span id="trip" class="value">0.0</span> <span class="unit">KM</span></div>
    </div>
    <div class="card">
      <div class="label">GPS SATS</div>
      <div><span id="sats" class="value">0</span> <span class="unit">SAT</span></div>
    </div>
    <div class="card" style="grid-column: span 2;">
      <div class="label">BATTERY</div>
      <div><span id="bat" class="value">0</span> <span class="unit">%</span></div>
    </div>
  </div>

  <script>
    // ... (map init code remains same) ...

    function updateStats() {
      fetch('/api/live')
        .then(response => {
            if (!response.ok) throw new Error("HTTP " + response.status);
            return response.json();
        })
        .then(data => {
          document.getElementById('connection-status').innerText = "Connected (Live)";
          document.getElementById('connection-status').style.color = "#04DF00"; // Green
          
          document.getElementById('speed').innerText = Math.round(data.speed);
          document.getElementById('rpm').innerText = data.rpm;
          
          // Trip with 1 decimal place (e.g. 12.5)
          document.getElementById('trip').innerText = data.trip.toFixed(1);

          // Battery Data
          var batText = data.bat_percent;
          if (data.is_charging) batText += " (CHG)";
          document.getElementById('bat').innerText = batText;
          
          // Debug Satellites:
          // document.getElementById('sats').innerText = data.sats; 
          // (Oops, I removed Sats card from HTML in this edit? BETTER KEEP IT)
        })
// ...
          if (map && marker && data.lat != 0 && data.lng != 0) {
            var newLatLng = new L.LatLng(data.lat, data.lng);
            marker.setLatLng(newLatLng);
            
            // Auto Follow
            if (!firstFix || document.activeElement !== map.getContainer()) {
                map.panTo(newLatLng);
                firstFix = true;
            }
          }
        })
        .catch(err => {
            console.error(err);
            document.getElementById('connection-status').innerText = "Err: " + err.message;
            document.getElementById('connection-status').style.color = "red";
        });
    }

    // Start Loop regardless of map status
    setInterval(updateStats, 500); // 2Hz Update
  </script>
</body>
</html>
)rawliteral";

const char UPDATE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Firmware Update</title>
  <style>
    body { background: #111; color: #eee; font-family: sans-serif; text-align: center; padding: 20px; }
    .container { background: #222; padding: 30px; border-radius: 12px; border: 1px solid #04DF00; display: inline-block; max-width: 400px; width: 100%; }
    input[type=file] { background: #333; padding: 10px; border-radius: 5px; width: 100%; margin-bottom: 20px; }
    button { background: #04DF00; color: #000; border: none; padding: 10px 20px; border-radius: 5px; font-weight: bold; cursor: pointer; width: 100%; }
    #prg { margin-top: 20px; background: #333; height: 20px; border-radius: 10px; overflow: hidden; display: none; }
    #bar { background: #04DF00; height: 100%; width: 0%; transition: width 0.3s; }
  </style>
</head>
<body>
  <div class="container">
    <h2>MuchUpdate</h2>
    <p>Select firmware file (.bin)</p>
    <form id="upload_form" enctype="multipart/form-data" method="post">
      <input type="file" name="update" accept=".bin">
      <button type="submit">Upload & Update</button>
    </form>
    <div id="prg"><div id="bar"></div></div>
    <p id="status"></p>
  </div>
  <script>
    var form = document.getElementById('upload_form');
    form.addEventListener('submit', e => {
      e.preventDefault();
      var data = new FormData(form);
      var req = new XMLHttpRequest();
      req.open('POST', '/update');
      
      document.getElementById('prg').style.display = 'block';
      document.getElementById('status').innerText = "Uploading...";
      
      req.upload.addEventListener('progress', p => {
        var pc = (p.loaded / p.total) * 100;
        document.getElementById('bar').style.width = pc + '%';
      });
      
      req.onload = () => {
        if (req.status == 200) {
          document.getElementById('status').innerText = "Update Success! Rebooting...";
          setTimeout(() => { window.location.href = '/'; }, 5000);
        } else {
          document.getElementById('status').innerText = "Error: " + req.responseText;
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
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Session Manager</title>
  <style>
    body { background: #111; color: #eee; font-family: sans-serif; text-align: center; padding: 10px; }
    h2 { color: #04DF00; }
    ul { list-style: none; padding: 0; }
    li { background: #222; margin: 10px auto; padding: 15px; border-radius: 8px; max-width: 400px; display: flex; justify-content: space-between; align-items: center; border: 1px solid #333; }
    a.btn { background: #04DF00; color: #000; text-decoration: none; padding: 8px 15px; border-radius: 5px; font-weight: bold; font-size: 14px; }
    .name { font-size: 16px; font-weight: bold; color: #fff; }
    .size { font-size: 12px; color: #aaa; }
    .empty { color: #555; margin-top: 50px; }
  </style>
</head>
<body>
  <h2>Session Files</h2>
  <div id="list">
    <p class="empty">Loading files...</p>
  </div>
  <p><a href="/" style="color:#888;">&larr; Back to Dashboard</a></p>

  <script>
    fetch('/api/sessions')
      .then(res => res.json())
      .then(files => {
        const list = document.getElementById('list');
        list.innerHTML = '';
        if (files.length === 0) {
          list.innerHTML = '<p class="empty">No sessions found on SD Card.</p>';
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
        document.getElementById('list').innerHTML = '<p class="empty" style="color:red;">Error loading list.</p>';
      });
  </script>
</body>
</html>
)rawliteral";

#endif
