#include "mgmt_handler.h"
#include "gateway.h"

MgmtHandler::MgmtHandler(Gateway* gateway) : gateway_(gateway) {}

void MgmtHandler::onMessage(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    printf("[MgmtHandler] onMessage, data=%.100s\n", data.c_str());
    if (data.find("STATS") == 0) {
        handleStats(conn);
    } else if (data.find("ROUTE_ADD") == 0) {
        std::string body = data.substr(10);
        handleRouteAdd(conn, body);
    } else {
        conn->send("{\"error\": \"unknown command\"}\n");
        printf("[MgmtHandler] unknown command sent\n");
    }
}

void MgmtHandler::handleStats(const std::shared_ptr<TcpConnection>& conn) {
    std::string json = gateway_->getStatsJson();
    printf("[MgmtHandler] Sending stats: %s\n", json.c_str());
    conn->send(json + "\n");
    printf("[MgmtHandler] Stats response sent\n");
}

void MgmtHandler::handleRouteAdd(const std::shared_ptr<TcpConnection>& conn, const std::string& body) {
    bool ok = gateway_->addRoute(body);
    conn->send(ok ? "{\"result\": \"ok\"}\n" : "{\"error\": \"failed\"}\n");
}
