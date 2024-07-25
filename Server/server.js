const net = require('net');
const http = require('http');
const fs = require('fs');
const path = require('path');
const WebSocket = require('ws');

const tcpPort = 12345;
const httpPort = 8080;
const clients = new Map();
const screens = new Map();

// Servidor TCP
const tcpServer = net.createServer((socket) => {
    console.log('Novo cliente conectado.');

    let computerName = '';
    let buffer = Buffer.alloc(0);

    socket.on('data', (data) => {
        buffer = Buffer.concat([buffer, data]);

        if (!computerName) {
            const nullTerminatorIndex = buffer.indexOf(0x00);
            if (nullTerminatorIndex !== -1) {
                computerName = buffer.slice(0, nullTerminatorIndex).toString();
                clients.set(socket, computerName);
                screens.set(computerName, '');
                buffer = buffer.slice(nullTerminatorIndex + 1); // Remove o nome do buffer
                console.log(`Cliente conectado: ${computerName}`);
                broadcastClients(); // Atualiza a lista de clientes
            }
        }

        if (computerName && buffer.length > 0) {
            const dataSize = buffer.readUInt32LE(0);
            if (buffer.length >= dataSize + 4) {
                const imageBuffer = buffer.slice(4, dataSize + 4);
                const base64Image = imageBuffer.toString('base64');
                screens.set(computerName, base64Image);
                broadcastScreens(computerName, base64Image);
                buffer = buffer.slice(dataSize + 4); // Remove a imagem processada do buffer
            }
        }
    });

    socket.on('end', () => {
        console.log(`Cliente desconectado: ${computerName}`);
        clients.delete(socket);
        screens.delete(computerName);
        broadcastClients(); // Atualiza a lista de clientes
        broadcastDisconnect(computerName); // Notifica desconexão
    });

    socket.on('error', (err) => {
        console.error(`Erro no cliente: ${err.message}`);
        clients.delete(socket);
        screens.delete(computerName);
        broadcastClients(); // Atualiza a lista de clientes
        broadcastDisconnect(computerName); // Notifica desconexão
    });
});

tcpServer.listen(tcpPort, '0.0.0.0', () => {
    console.log(`Servidor TCP rodando em 0.0.0.0:${tcpPort}`);
});

// Servidor HTTP
const httpServer = http.createServer((req, res) => {
    if (req.url === '/') {
        const indexPath = path.join(__dirname, 'index.html');
        fs.readFile(indexPath, (err, data) => {
            if (err) {
                res.writeHead(500);
                res.end('Erro ao ler o arquivo index.html');
            } else {
                res.writeHead(200, { 'Content-Type': 'text/html' });
                res.end(data);
            }
        });
    } else if (req.url === '/style.css') {
        const cssPath = path.join(__dirname, 'style.css');
        fs.readFile(cssPath, (err, data) => {
            if (err) {
                res.writeHead(404);
                res.end("404 Not Found");
            } else {
                res.writeHead(200, { 'Content-Type': 'text/css' });
                res.end(data);
            }
        });
    } else if (req.url === '/script.js') {
        const jsPath = path.join(__dirname, 'script.js');
        fs.readFile(jsPath, (err, data) => {
            if (err) {
                res.writeHead(404);
                res.end("404 Not Found");
            } else {
                res.writeHead(200, { 'Content-Type': 'application/javascript' });
                res.end(data);
            }
        });
    } else {
        res.writeHead(404);
        res.end("404 Not Found");
    }
});

httpServer.listen(httpPort, '0.0.0.0', () => {
    console.log(`Servidor HTTP rodando em 0.0.0.0:${httpPort}`);
});

// Servidor WebSocket
const wss = new WebSocket.Server({ server: httpServer });

wss.on('connection', (ws) => {
    ws.send(JSON.stringify({
        type: 'clients',
        clients: Array.from(clients.values())
    }));

    ws.on('message', (message) => {
        console.log(`Recebido: ${message}`);
    });
});

function broadcastClients() {
    const clientList = JSON.stringify({
        type: 'clients',
        clients: Array.from(clients.values())
    });
    wss.clients.forEach((client) => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(clientList);
        }
    });
}

function broadcastScreens(computerName, screenData) {
    const screenMessage = JSON.stringify({
        type: 'screen',
        computerName: computerName,
        screenData: screenData
    });
    wss.clients.forEach((client) => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(screenMessage);
        }
    });
}

function broadcastDisconnect(computerName) {
    const disconnectMessage = JSON.stringify({
        type: 'disconnect',
        computerName: computerName
    });
    wss.clients.forEach((client) => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(disconnectMessage);
        }
    });
}
