#ifndef MGMT_HANDLER_H
#define MGMT_HANDLER_H

#include <memory>
#include <string>

class TcpConnection;
class Gateway;

class MgmtHandler {
public:
    explicit MgmtHandler(Gateway* gateway);
    void onMessage(const std::shared_ptr<TcpConnection>& conn, std::string& data);

private:
    void handleStats(const std::shared_ptr<TcpConnection>& conn);
    void handleRouteList(const std::shared_ptr<TcpConnection>& conn);
    void handleRouteAdd(const std::shared_ptr<TcpConnection>& conn, const std::string& body);
    void handleRouteDelete(const std::shared_ptr<TcpConnection>& conn, const std::string& body);
    void handleLogQuery(const std::shared_ptr<TcpConnection>& conn, const std::string& body);
    Gateway* gateway_;
};

#endif
