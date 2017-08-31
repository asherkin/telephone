const WebSocket = require('ws');

const wss = new WebSocket.Server({ port: 9001 });

wss.on('connection', function connection(ws) {
  ws.on('message', function incoming(message) {
    console.log('received %d bytes', message.length);
  });

  ws.send('hello from relay server');
});
