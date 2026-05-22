#include "mgmt_handler.h"
#include "gateway.h"

MgmtHandler::MgmtHandler(Gateway* gateway) : gateway_(gateway) {}

void MgmtHandler::onMessage(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    printf("[MgmtHandler] onMessage, data=%.100s\n", data.c_str());
    if (data.find("STATS") == 0) {
        handleStats(conn);
    } else if (data.find("HEALTH") == 0) {
        conn->send(gateway_->getHealthJson() + "\n");
    } else if (data.find("ROUTE_LIST") == 0) {
        handleRouteList(conn);
    } else if (data.find("ROUTE_ADD") == 0) {
        std::string body = data.substr(10);
        handleRouteAdd(conn, body);
    } else if (data.find("ROUTE_DELETE") == 0) {
        std::string body = data.substr(13);
        handleRouteDelete(conn, body);
    } else if (data.find("LOG_QUERY") == 0) {
        std::string body = data.substr(10);
        handleLogQuery(conn, body);
    } else {
        conn->send("{\"error\": \"unknown command\"}\n");
    }
}

void MgmtHandler::handleStats(const std::shared_ptr<TcpConnection>& conn) {
    conn->send(gateway_->getStatsJson() + "\n");
}

void MgmtHandler::handleRouteList(const std::shared_ptr<TcpConnection>& conn) {
    conn->send(gateway_->getRouteListJson() + "\n");
}

void MgmtHandler::handleRouteAdd(const std::shared_ptr<TcpConnection>& conn, const std::string& body) {
    bool ok = gateway_->addRoute(body);
    conn->send(ok ? "{\"result\": \"ok\"}\n" : "{\"error\": \"failed\"}\n");
}

void MgmtHandler::handleRouteDelete(const std::shared_ptr<TcpConnection>& conn, const std::string& body) {
    bool ok = gateway_->deleteRoute(body);
    conn->send(ok ? "{\"result\": \"ok\"}\n" : "{\"error\": \"failed\"}\n");
}

void MgmtHandler::handleLogQuery(const std::shared_ptr<TcpConnection>& conn, const std::string& body) {
    conn->send(gateway_->getLogsJson(body) + "\n");
}
