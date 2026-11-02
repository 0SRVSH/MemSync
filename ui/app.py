import streamlit as st
import mysql.connector
import pandas as pd

from benchmark_view import render_benchmark_lab
from cache_view import render_cache_explorer
from project_sidebar import render_project_sidebar
from test_view import render_test_lab

st.set_page_config(page_title="MemSync Console", page_icon="🧠", layout="wide")
st.title("MemSync Console")
st.caption("Explore the shared-memory NoSQL cache, run tests, benchmark performance, and monitor MySQL relational telemetry.")

build_dir = render_project_sidebar()

# Expanded tabs to include the MySQL Relational Auditing Layer
cache_tab, tests_tab, benchmark_tab, db_audit_tab = st.tabs(
    ["Cache Explorer", "Test Lab", "Benchmark", "DBMS Audit & Telemetry"]
)

with cache_tab:
    render_cache_explorer(build_dir)

with tests_tab:
    render_test_lab(build_dir)

with benchmark_tab:
    render_benchmark_lab(build_dir)

with db_audit_tab:
    st.header("🗄️ MySQL Relational Audit & Telemetry")
    st.markdown("Monitoring database integration: **Normalization, Foreign Keys, SQL Views, and Automated Triggers**.")

    # Database connection helper
    def get_db_connection():
        return mysql.connector.connect(
            host="localhost",
            user="memsync_user",
            password="memsync_pass",
            database="memsync_db"
        )

    # Control buttons row for refreshing or wiping old logs
    col_ctrl1, col_ctrl2 = st.columns([1, 4])
    with col_ctrl1:
        if st.button("🔄 Refresh Data"):
            st.rerun()
    with col_ctrl2:
        if st.button("🗑️ Clear All Audit Logs"):
            try:
                conn_clear = get_db_connection()
                cursor_clear = conn_clear.cursor()
                cursor_clear.execute("DELETE FROM ProcessExecutionLogs;")
                cursor_clear.execute("DELETE FROM AuditAlerts;")
                conn_clear.commit()
                cursor_clear.close()
                conn_clear.close()
                st.success("Successfully cleared all MySQL process execution logs and audit alerts!")
                st.rerun()
            except Exception as ex:
                st.error(f"Failed to clear database logs: {ex}")

    try:
        conn = get_db_connection()

        # --- Section 1: Live Process Execution Logs ---
        st.subheader("📊 Live Process Execution Logs (Transactional Table)")
        df_logs = pd.read_sql(
            "SELECT log_id, pid, status, cpu_usage_pct, memory_footprint_kb, logged_at FROM ProcessExecutionLogs ORDER BY logged_at DESC LIMIT 10;", 
            conn
        )
        if not df_logs.empty:
            st.dataframe(df_logs, use_container_width=True)
        else:
            st.info("No execution logs found. Try running `./memsync_writer` in your WSL terminal!")

        # Two-column layout for advanced DBMS features
        col1, col2 = st.columns(2)

        with col1:
            # --- Section 2: High Memory Processes (SQL View) ---
            st.subheader("🔥 High-Memory Processes (SQL View)")
            df_view = pd.read_sql("SELECT * FROM HighMemoryProcesses;", conn)
            if not df_view.empty:
                st.dataframe(df_view, use_container_width=True)
            else:
                st.info("No active processes exceeding the 50MB threshold.")

        with col2:
            # --- Section 3: System Audit Alerts (SQL Trigger Logs) ---
            st.subheader("🚨 System Audit Alerts (Trigger Logs)")
            df_alerts = pd.read_sql("SELECT message, triggered_at FROM AuditAlerts ORDER BY triggered_at DESC LIMIT 5;", conn)
            if not df_alerts.empty:
                for _, row in df_alerts.iterrows():
                    st.warning(f"**[{row['triggered_at']}]** {row['message']}")
            else:
                st.success("✅ System stable: No critical CPU spikes caught by triggers.")
    
        conn.close()

    except Exception as e:
        st.error(f"Database Connection Error: {e}")
        st.info("💡 **Tip:** Ensure MySQL is running in your WSL terminal using `sudo service mysql start` and that your user credentials are correct.")