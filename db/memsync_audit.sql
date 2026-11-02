-- Create database
CREATE DATABASE IF NOT EXISTS memsync_db;
USE memsync_db;

-- 1. Master Table: Normalized process metadata (3rd Normal Form)
CREATE TABLE IF NOT EXISTS ProcessMaster (
    pid INT PRIMARY KEY,
    process_name VARCHAR(100) NOT NULL,
    owner_user VARCHAR(50) NOT NULL,
    spawn_timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 2. Transactional Log Table: Runtime metrics linked via Foreign Key with Cascade Delete
CREATE TABLE IF NOT EXISTS ProcessExecutionLogs (
    log_id INT AUTO_INCREMENT PRIMARY KEY,
    pid INT,
    status ENUM('RUNNING', 'IDLE', 'TERMINATED', 'ZOMBIE') NOT NULL,
    cpu_usage_pct DECIMAL(5,2),
    memory_footprint_kb INT,
    logged_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (pid) REFERENCES ProcessMaster(pid) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 3. SQL View: Virtual table for quick analysis of high-memory processes (DBMS Unit 3: Views)
CREATE OR REPLACE VIEW HighMemoryProcesses AS
SELECT 
    p.pid,
    p.process_name,
    l.memory_footprint_kb,
    l.logged_at
FROM ProcessMaster p
JOIN ProcessExecutionLogs l ON p.pid = l.pid
WHERE l.memory_footprint_kb > 50000
ORDER BY l.memory_footprint_kb DESC;

-- 4. Audit Alerts Table for Triggers
CREATE TABLE IF NOT EXISTS AuditAlerts (
    alert_id INT AUTO_INCREMENT PRIMARY KEY,
    message TEXT,
    triggered_at DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 5. SQL Trigger: Automatically logs critical events when CPU spikes (DBMS Unit 3: Triggers)
DELIMITER //

CREATE TRIGGER IF NOT EXISTS CpuSpikeTrigger
AFTER INSERT ON ProcessExecutionLogs
FOR EACH ROW
BEGIN
    IF NEW.cpu_usage_pct > 85.0 THEN
        INSERT INTO AuditAlerts (message) 
        VALUES (CONCAT('CRITICAL: Process ID ', NEW.pid, ' spiked to ', NEW.cpu_usage_pct, '% CPU.'));
    END IF;
END//

DELIMITER ;






-- Stored Procedure for Safe Batch Logging with Transactions (DBMS Unit 3 & 5)
DELIMITER //

CREATE PROCEDURE LogProcessActivity(
    IN p_pid INT,
    IN p_name VARCHAR(100),
    IN p_user VARCHAR(50),
    IN p_status VARCHAR(20),
    IN p_cpu DECIMAL(5,2),
    IN p_mem INT
)
BEGIN
    -- Declare exit handler for rollback on SQL errors (ACID Atomicity)
    DECLARE EXIT HANDLER FOR SQLEXCEPTION 
    BEGIN
        ROLLBACK;
    END;

    START TRANSACTION;

        -- Upsert process master
        INSERT INTO ProcessMaster (pid, process_name, owner_user) 
        VALUES (p_pid, p_name, p_user)
        ON DUPLICATE KEY UPDATE process_name = VALUES(process_name);

        -- Insert telemetry log
        INSERT INTO ProcessExecutionLogs (pid, status, cpu_usage_pct, memory_footprint_kb) 
        VALUES (p_pid, p_status, p_cpu, p_mem);

    COMMIT;
END//

DELIMITER ;