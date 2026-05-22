#include "../gateway/include/gateway.h"
#include <csignal>

EventLoop* g_loop = nullptr;
Gateway* g_gw = nullptr;

void handleSig(int sig) {
    printf("\n[Gateway] Signal %d, stopping...\n", sig);
    if (g_gw) g_gw->stop();
    if (g_loop) g_loop->quit();
}

int main() {
    signal(SIGINT, handleSig);
    signal(SIGTERM, handleSig);

    EventLoop loop;
    g_loop = &loop;

    Gateway gw(&loop, 8080, 8081, 16,
               "localhost", "gateway", "gateway123", "lfgateway",
               "localhost", 6379);
    g_gw = &gw;
    gw.start();

    printf("[Gateway] Running... Ctrl+C to stop\n");
    loop.loop();
    printf("[Gateway] Bye!\n");
    return 0;
}
