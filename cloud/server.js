require('dotenv').config();
const { WebSocketServer } = require('ws');

const PORT = process.env.PORT || 8080;
const pairs = new Map();

function getOrCreatePair(pairId) {
  if (!pairs.has(pairId)) {
    pairs.set(pairId, { deviceA: null, deviceB: null, lastState: false, lastSeenA: 0, lastSeenB: 0 });
  }
  return pairs.get(pairId);
}

function safeSend(ws, obj) {
  if (ws && ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
}

const wss = new WebSocketServer({ port: PORT });

wss.on('connection', (ws) => {
  let boundPairId = null;
  let boundRole = null;

  ws.on('message', (raw) => {
    let msg;
    try { msg = JSON.parse(raw.toString()); } catch (e) { return safeSend(ws, { type: 'error', message: 'invalid_json' }); }

    if (msg.type === 'hello') {
      const { role, pair_id, token } = msg;
      if (role !== 'A' && role !== 'B') return safeSend(ws, { type: 'error', message: 'invalid_role' });
      if (!pair_id) return safeSend(ws, { type: 'error', message: 'missing_pair_id' });
      if (!token) return safeSend(ws, { type: 'error', message: 'missing_token' });

      const pair = getOrCreatePair(pair_id);
      boundPairId = pair_id;
      boundRole = role;
      if (role === 'A') pair.deviceA = ws;
      if (role === 'B') pair.deviceB = ws;
      safeSend(ws, { type: 'hello_ack', pair_id, role });
      console.log(`Device ${role} connected to pair ${pair_id}`);
      return;
    }

    if (!boundPairId) return safeSend(ws, { type: 'error', message: 'not_registered_send_hello_first' });
    const pair = getOrCreatePair(boundPairId);

    if (msg.type === 'state' && boundRole === 'A') {
      const on = !!msg.on;
      pair.lastState = on;
      pair.lastSeenA = Date.now();
      safeSend(pair.deviceB, { type: 'target_state', on, ts: Date.now() });
      console.log(`Pair ${boundPairId}: state -> ${on}`);
      return;
    }
  });

  ws.on('close', () => {
    if (boundPairId) {
      const pair = pairs.get(boundPairId);
      if (pair) {
        if (boundRole === 'A' && pair.deviceA === ws) pair.deviceA = null;
        if (boundRole === 'B' && pair.deviceB === ws) pair.deviceB = null;
      }
      console.log(`Device ${boundRole} disconnected from pair ${boundPairId}`);
    }
  });
});

console.log(`Kettle-Sync relay server (v2 - simple on/off) listening on ws://localhost:${PORT}`);