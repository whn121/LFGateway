#!/bin/bash
# ============================================
# LFGateway 性能压测脚本
# 环境：4核8G虚拟机，Ubuntu 22.04，本地回环
# 工具：wrk 4.2.0
# 用法：bash benchmark.sh
# 注意：运行前请确保网关已启动，并且 Redis 中已设置 token:test123
#       redis-cli SET token:test123 "admin"
# ============================================

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # 无颜色

# 检查 wrk 是否安装
if ! command -v wrk &> /dev/null; then
    echo -e "${RED}错误：wrk 未安装。请先执行 sudo apt install wrk${NC}"
    exit 1
fi

# 检查网关是否在运行
if ! nc -z localhost 8080 2>/dev/null; then
    echo -e "${RED}错误：网关未启动。请先在另一个终端执行 ./gateway${NC}"
    exit 1
fi

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}   LFGateway 性能压测报告${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo -e "测试环境：4核8G虚拟机 | Ubuntu 22.04 | 本地回环"
echo -e "测试工具：wrk 4.2.0"
echo -e "测试时间：$(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# ========== 测试1：基础吞吐量测试 ==========
echo -e "${YELLOW}[测试1] 基础吞吐量测试 (4线程, 100连接, 10秒)${NC}"
echo "命令: wrk -t4 -c100 -d10s http://localhost:8080/api/test -H 'Authorization: Bearer test123'"
echo "----------------------------------------"
wrk -t4 -c100 -d10s http://localhost:8080/api/test \
    -H "Authorization: Bearer test123"
echo ""
echo ""

# ========== 测试2：高并发压力测试 ==========
echo -e "${YELLOW}[测试2] 高并发压力测试 (4线程, 500连接, 30秒)${NC}"
echo "命令: wrk -t4 -c500 -d30s http://localhost:8080/api/test -H 'Authorization: Bearer test123'"
echo "----------------------------------------"
wrk -t4 -c500 -d30s http://localhost:8080/api/test \
    -H "Authorization: Bearer test123"
echo ""
echo ""

# ========== 测试3：查看网关运行状态 ==========
echo -e "${YELLOW}[测试3] 网关运行统计 (STATS)${NC}"
echo "命令: echo 'STATS' | nc -N localhost 8081"
echo "----------------------------------------"
echo "STATS" | timeout 2 nc localhost 8081 2>/dev/null
echo ""
echo ""

# ========== 测试4：查看连接池健康状态 ==========
echo -e "${YELLOW}[测试4] MySQL连接池健康检查 (HEALTH)${NC}"
echo "命令: echo 'HEALTH' | nc -N localhost 8081"
echo "----------------------------------------"
echo "HEALTH" | timeout 2 nc localhost 8081 2>/dev/null
echo ""
echo ""

# ========== 测试5：限流验证 ==========
echo -e "${YELLOW}[测试5] 限流功能验证 (连续1500次请求)${NC}"
echo "命令: for i in \$(seq 1 1500); do curl -s ... | grep -c 'rate limit exceeded'"
echo "----------------------------------------"
LIMITED_COUNT=0
SUCCESS_COUNT=0
for i in $(seq 1 1500); do
    RESULT=$(curl -s -H "Authorization: Bearer test123" \
             http://localhost:8080/api/test 2>/dev/null)
    if echo "$RESULT" | grep -q "rate limit exceeded"; then
        LIMITED_COUNT=$((LIMITED_COUNT + 1))
    elif echo "$RESULT" | grep -q "OK"; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    fi
done
echo "  成功请求: $SUCCESS_COUNT"
echo "  被限流请求: $LIMITED_COUNT"
echo ""

# ========== 测试6：Redis Streams 日志验证 ==========
echo -e "${YELLOW}[测试6] Redis Streams 异步日志验证${NC}"
echo "命令: redis-cli XLEN log_stream"
echo "----------------------------------------"
STREAM_LEN=$(redis-cli XLEN log_stream 2>/dev/null | tr -d '()')
echo "  log_stream 消息数: $STREAM_LEN"
echo ""

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}   压测完成！${NC}"
echo -e "${GREEN}============================================${NC}"
