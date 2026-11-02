#ifndef MYSQL_LOGGER_H
#define MYSQL_LOGGER_H

#include <mysql/mysql.h>
#include <iostream>
#include <string>

class MySQLLogger {
private:
    MYSQL* conn;
    bool is_connected;

public:
    MySQLLogger(const char* host, const char* user, const char* pass, const char* db, unsigned int port = 3306) {
        conn = mysql_init(nullptr);
        if (!conn) {
            std::cerr << "[MySQL Error] mysql_init failed" << std::endl;
            is_connected = false;
            return;
        }

        if (mysql_real_connect(conn, host, user, pass, db, port, nullptr, 0)) {
            is_connected = true;
            std::cout << "[MySQL] Connected to database successfully." << std::endl;
        } else {
            std::cerr << "[MySQL Error] Connection failed: " << mysql_error(conn) << std::endl;
            is_connected = false;
        }
    }

    ~MySQLLogger() {
        if (conn) {
            mysql_close(conn);
        }
    }

    bool logProcessViaStoredProcedure(int pid, const std::string& name, const std::string& user, 
                                      const std::string& status, double cpu, int mem_kb) {
        if (!is_connected) return false;

        std::string query = "CALL LogProcessActivity(" + 
                            std::to_string(pid) + ", '" + 
                            name + "', '" + 
                            user + "', '" + 
                            status + "', " + 
                            std::to_string(cpu) + ", " + 
                            std::to_string(mem_kb) + ");";

        if (mysql_query(conn, query.c_str())) {
            std::cerr << "[MySQL Error] Stored procedure execution failed: " << mysql_error(conn) << std::endl;
            return false;
        }
        return true;
    }
};

#endif // MYSQL_LOGGER_H