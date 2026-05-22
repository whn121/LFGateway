CREATE TABLE IF NOT EXISTS api_routes (
    id INT AUTO_INCREMENT PRIMARY KEY,
    path_pattern VARCHAR(256) NOT NULL,
    target_host VARCHAR(256) NOT NULL,
    target_port INT NOT NULL,
    need_auth TINYINT DEFAULT 1,
    rate_limit INT DEFAULT 1000
);

CREATE TABLE IF NOT EXISTS api_call_logs (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    path VARCHAR(256),
    user_id INT,
    status_code INT,
    latency_ms DOUBLE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO api_routes (path_pattern, target_host, target_port, need_auth, rate_limit)
VALUES ('/api/test', 'localhost', 8082, 1, 10);
