#!/usr/bin/env node
const mqtt = require('mqtt');

function ts() {
  const d = new Date();
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  const ss = String(d.getSeconds()).padStart(2, '0');
  return `${y}-${m}-${day} ${hh}:${mm}:${ss}`;
}

const MQTT_URL = process.env.MQTT_URL || 'mqtt://127.0.0.1:1883';
const MQTT_USERNAME = process.env.MQTT_USERNAME || '';
const MQTT_PASSWORD = process.env.MQTT_PASSWORD || '';
const MQTT_TOPIC_PREFIX = process.env.MQTT_TOPIC_PREFIX || 'wms';
const DEVICE_ID = process.env.SIM_DEVICE_ID || 'WMS_99999';
const CLAIM_CODE = process.env.SIM_CLAIM_CODE || DEVICE_ID;

const base = `${MQTT_TOPIC_PREFIX}/${DEVICE_ID}`;

let zones = Array.from({ length: 8 }, (_, i) => ({
  id: i,
  active: false,
  remaining: 0,
  name: `Strefa ${i + 1}`,
}));
let programs = [];
let logs = { logs: [`${ts()}: [SIM] Symulator uruchomiony`] };
let settingsPublic = {
  timezone: '+01:00',
  enableWeatherApi: true,
  weatherUpdateInterval: 60,
  enableMqtt: true,
  autoMode: true,
  mqttServer: 'wmsprinkler.pl',
  mqttBroker: 'wmsprinkler.pl',
  mqttPort: 8883,
  mqttUser: MQTT_USERNAME || 'wms_device',
  mqttTopic: base,
  mqttTopicBase: base,
  mqttClientId: `wm-sim-${DEVICE_ID.toLowerCase()}`,
};
let rainHistory = [];
let wateringPercent = {
  percent: 100,
  allowed: true,
  hard_stop: false,
  hard_stop_reason_code: 'none',
  hard_stop_reason: '',
  rain_24h: 0,
  temp_now: 21.5,
  humidity_now: 54,
  wind_now_kmh: 8.6,
  factor_temp: 1.0,
  factor_rain: 1.0,
  factor_humidity: 1.0,
  factor_wind: 1.0,
  factor_total: 1.0,
  percent_min: 0,
  percent_max: 160,
  daily_max_temp: 22,
  daily_humidity_forecast: 55,
};
let weather = {
  temp: '21.5',
  humidity: 54,
  rain: '0.0',
  rain_1h_forecast: '0.0',
  rain_24h_forecast: '0.0',
  clouds: '20',
  wind: '8',
  description: 'bezchmurnie',
  daily_max_temp: 23,
  daily_humidity_forecast: 56,
  watering_percent: 100,
};

function addLog(msg) {
  logs.logs.push(`${ts()}: ${msg}`);
  if (logs.logs.length > 50) logs.logs = logs.logs.slice(-50);
}

function pub(topic, payload, retain = true) {
  const msg = typeof payload === 'string' ? payload : JSON.stringify(payload);
  client.publish(`${base}/${topic}`, msg, { retain });
}

function pubStatus() {
  pub('global/status', {
    wifi: 'Połączono',
    ip: '192.168.1.120',
    time: ts().slice(0, 16),
    online: true,
    device_id: DEVICE_ID,
    claim_code: CLAIM_CODE,
  });
}

function pubAll() {
  pubStatus();
  pub('weather', weather);
  pub('zones', zones);
  pub('programs', programs);
  pub('logs', logs);
  pub('settings/public', settingsPublic);
  pub('rain-history', rainHistory);
  pub('watering-percent', wateringPercent);
  zones.forEach((z) => {
    pub(`zones/${z.id}/status`, z.active ? '1' : '0');
    pub(`zones/${z.id}/remaining`, String(z.remaining));
  });
}

function ack(commandId, commandTopic, status, detail) {
  pub('ack', {
    command_id: commandId || `legacy_${Date.now()}`,
    command_topic: commandTopic,
    status,
    detail,
    timestamp: ts(),
  }, false);
}

function parsePayload(text) {
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

function onCommand(topic, payloadText) {
  const cmd = topic.replace(`${base}/`, '');
  const json = parsePayload(payloadText);
  const commandId = json?.command_id || `legacy_${Date.now()}`;

  if (cmd === 'global/refresh') {
    pubAll();
    ack(commandId, cmd, 'accepted', 'refresh wykonany');
    return;
  }

  if (cmd.startsWith('cmd/zones/')) {
    const m = cmd.match(/^cmd\/zones\/(\d+)\/(toggle|start|stop)$/);
    if (!m) return ack(commandId, cmd, 'failed', 'nieprawidlowy topic');
    const id = Number(m[1]);
    const action = m[2];
    if (id < 0 || id > 7) return ack(commandId, cmd, 'failed', 'nieprawidlowe id strefy');

    if (action === 'toggle') {
      zones[id].active = !zones[id].active;
      zones[id].remaining = zones[id].active ? 600 : 0;
      addLog(`MQTT CMD: toggle strefa ${id + 1}`);
      pub('zones', zones);
      ack(commandId, cmd, 'accepted', 'przelaczono strefe');
      return;
    }
    if (action === 'start') {
      const secs = Number(json?.seconds || 600);
      zones[id].active = true;
      zones[id].remaining = secs > 0 ? secs : 600;
      addLog(`MQTT CMD: start strefa ${id + 1} na ${zones[id].remaining}s`);
      pub('zones', zones);
      ack(commandId, cmd, 'accepted', 'uruchomiono strefe');
      return;
    }
    zones[id].active = false;
    zones[id].remaining = 0;
    addLog(`MQTT CMD: stop strefa ${id + 1}`);
    pub('zones', zones);
    ack(commandId, cmd, 'accepted', 'zatrzymano strefe');
    return;
  }

  if (cmd === 'cmd/zones-names/set') {
    const arr = json?.names;
    if (!Array.isArray(arr)) return ack(commandId, cmd, 'failed', 'brak tablicy names');
    arr.forEach((name, i) => {
      if (i < zones.length) zones[i].name = String(name || `Strefa ${i + 1}`);
    });
    addLog('MQTT CMD: zmieniono nazwy stref');
    pub('zones', zones);
    ack(commandId, cmd, 'accepted', 'nazwy stref zapisane');
    return;
  }

  if (cmd === 'cmd/programs/import') {
    const arr = json?.programs;
    if (!Array.isArray(arr)) return ack(commandId, cmd, 'failed', 'brak tablicy programow');
    programs = arr;
    addLog('MQTT CMD: import programow');
    pub('programs', programs);
    ack(commandId, cmd, 'accepted', 'programy zaimportowane');
    return;
  }

  if (cmd === 'cmd/programs/add') {
    const p = json?.program;
    if (!p || typeof p !== 'object') return ack(commandId, cmd, 'failed', 'brak programu');
    programs.push(p);
    addLog('MQTT CMD: dodaj program');
    pub('programs', programs);
    ack(commandId, cmd, 'accepted', 'program zapisany');
    return;
  }

  if (cmd.startsWith('cmd/programs/edit/')) {
    const id = Number(cmd.split('/').pop());
    const p = json?.program;
    if (!Number.isInteger(id) || id < 0 || !p || typeof p !== 'object') {
      return ack(commandId, cmd, 'failed', 'bledna edycja programu');
    }
    programs[id] = { ...(programs[id] || {}), ...p };
    addLog(`MQTT CMD: edytuj program ${id}`);
    pub('programs', programs);
    ack(commandId, cmd, 'accepted', 'program zaktualizowany');
    return;
  }

  if (cmd.startsWith('cmd/programs/delete/')) {
    const id = Number(cmd.split('/').pop());
    if (!Number.isInteger(id) || id < 0 || id >= programs.length) {
      return ack(commandId, cmd, 'failed', 'bledne id programu');
    }
    programs.splice(id, 1);
    addLog(`MQTT CMD: usun program ${id}`);
    pub('programs', programs);
    ack(commandId, cmd, 'accepted', 'program usuniety');
    return;
  }

  if (cmd === 'cmd/logs/clear') {
    logs.logs = [`${ts()}: MQTT CMD: wyczyszczono logi`];
    pub('logs', logs);
    ack(commandId, cmd, 'accepted', 'logi wyczyszczone');
    return;
  }

  if (cmd === 'cmd/settings/set') {
    const s = json?.settings;
    if (!s || typeof s !== 'object') return ack(commandId, cmd, 'failed', 'brak ustawien');
    settingsPublic = { ...settingsPublic, ...s };
    addLog('MQTT CMD: zapisano ustawienia');
    pub('settings/public', settingsPublic);
    ack(commandId, cmd, 'accepted', 'ustawienia zapisane');
    return;
  }

  ack(commandId, cmd, 'failed', 'nieobslugiwana komenda');
}

const client = mqtt.connect(MQTT_URL, {
  username: MQTT_USERNAME || undefined,
  password: MQTT_PASSWORD || undefined,
  reconnectPeriod: 1000,
});

client.on('connect', () => {
  console.log(`[SIM] connected to ${MQTT_URL}`);
  console.log(`[SIM] device_id=${DEVICE_ID}, claim_code=${CLAIM_CODE}`);
  client.subscribe(`${base}/global/refresh`);
  client.subscribe(`${base}/cmd/#`);
  pubAll();
});

client.on('message', (topic, payload) => {
  onCommand(topic, payload.toString('utf8'));
});

setInterval(() => {
  // Podtrzymanie online + odliczanie remaining
  zones = zones.map((z) => {
    if (!z.active) return z;
    const next = { ...z, remaining: Math.max(0, z.remaining - 10) };
    if (next.remaining === 0) next.active = false;
    return next;
  });
  pubStatus();
  pub('zones', zones);
}, 10_000);

process.on('SIGINT', () => {
  console.log('[SIM] stopping...');
  client.end(true, () => process.exit(0));
});
