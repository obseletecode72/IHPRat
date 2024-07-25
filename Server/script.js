const ws = new WebSocket(`ws://${window.location.hostname}:8080`);
const activeClients = new Set();

ws.onmessage = (event) => {
    const message = JSON.parse(event.data);

    if (message.type === 'clients') {
        const clients = message.clients;
        const clientList = document.getElementById('client-list');
        clientList.innerHTML = '';
        clients.forEach((client) => {
            const li = document.createElement('li');
            li.textContent = client;

            const button = document.createElement('button');
            button.classList.add('screen-button');

            // Adiciona o ícone SVG ao botão
            button.innerHTML = `
                <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 576 512">
                    <path d="M64 0C28.7 0 0 28.7 0 64L0 352c0 35.3 28.7 64 64 64l176 0-10.7 32L160 448c-17.7 0-32 14.3-32 32s14.3 32 32 32l256 0c17.7 0 32-14.3 32-32s-14.3-32-32-32l-69.3 0L336 416l176 0c35.3 0 64-28.7 64-64l0-288c0-35.3-28.7-64-64-64L64 0zM512 64l0 224L64 288 64 64l448 0z"/>
                </svg>
                Tela
            `;
            button.onclick = () => toggleScreenshot(client);
            li.appendChild(button);
            clientList.appendChild(li);

            // Mostrar notificação para novos clientes
            showNotification(client);
        });
    } else if (message.type === 'screen') {
        const { computerName, screenData } = message;
        if (activeClients.has(computerName)) {
            let img = document.getElementById(`screenshot-${computerName}`);
            if (!img) {
                img = document.createElement('img');
                img.id = `screenshot-${computerName}`;
                img.src = `data:image/jpeg;base64,${screenData}`;
                const screenshotsDiv = document.getElementById('screenshots');
                screenshotsDiv.appendChild(img);
            } else {
                img.src = `data:image/jpeg;base64,${screenData}`;
            }
        }
    } else if (message.type === 'disconnect') {
        const { computerName } = message;
        activeClients.delete(computerName);
        const img = document.getElementById(`screenshot-${computerName}`);
        if (img) {
            img.remove();
        }
    }
};

function toggleScreenshot(computerName) {
    if (activeClients.has(computerName)) {
        // Se o cliente já está ativo, remova-o
        activeClients.delete(computerName);
        const img = document.getElementById(`screenshot-${computerName}`);
        if (img) {
            img.remove();
        }
    } else {
        // Se o cliente não está ativo, adicione-o
        activeClients.add(computerName);
        const img = document.createElement('img');
        img.id = `screenshot-${computerName}`;
        const screenshotsDiv = document.getElementById('screenshots');
        screenshotsDiv.appendChild(img);
    }
}

document.getElementById('dark-mode-toggle').addEventListener('click', function() {
    document.body.classList.toggle('dark-mode');
});

function showNotification(clientName) {
    const notificationsContainer = document.getElementById('notifications-container');
    const notification = document.createElement('div');
    notification.classList.add('notification');
    notification.textContent = `Cliente conectado: ${clientName}`;
    notificationsContainer.appendChild(notification);

    setTimeout(() => {
        notification.remove();
    }, 3000); // Duração da notificação
}
