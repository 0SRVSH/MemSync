import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import streamlit as st


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD_DIR = ROOT / "build"
TEST_LINE = re.compile(r"^\s*Test\s+#\d+:\s+(.+?)\s*$")

st.set_page_config(page_title="MemSync Console", page_icon="🧠", layout="wide")
st.title("MemSync Console")
st.caption("Explore the shared-memory cache and run its GoogleTest suite.")


def run_command(command, cwd=ROOT, timeout=30):
    return subprocess.run(
        command,
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def bridge_path(build_dir):
    executable = "memsync_ui_bridge.exe" if sys.platform == "win32" else "memsync_ui_bridge"
    return build_dir / executable


def cleanup_path(build_dir):
    executable = "memsync_cleanup.exe" if sys.platform == "win32" else "memsync_cleanup"
    return build_dir / executable


def call_bridge(build_dir, *arguments):
    executable = bridge_path(build_dir)
    if not executable.is_file():
        raise FileNotFoundError(
            f"Cache bridge not found at {executable}. Configure and build the project first."
        )
    try:
        result = run_command([str(executable), *map(str, arguments)])
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Cache operation timed out after 30 seconds.") from error
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "Cache operation failed.")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"Cache bridge returned invalid output: {error}") from error


def cleanup_cache(build_dir):
    executable = cleanup_path(build_dir)
    if not executable.is_file():
        raise FileNotFoundError(
            f"Cache cleanup utility not found at {executable}. Configure and build the project first."
        )
    try:
        result = run_command([str(executable)])
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Cache cleanup timed out after 30 seconds.") from error
    if result.returncode != 0:
        raise RuntimeError(
            result.stderr.strip() or result.stdout.strip() or "Cache cleanup failed."
        )
    return result.stdout.strip() or "Cache cleanup completed."


def ctest_executable():
    executable = shutil.which("ctest")
    if executable is None:
        raise FileNotFoundError("ctest is not on PATH. Run the UI in the Linux/WSL build environment.")
    return executable


def discover_tests(build_dir):
    if not (build_dir / "CTestTestfile.cmake").is_file():
        raise FileNotFoundError(
            f"No CTest configuration found in {build_dir}. Configure and build with CMake first."
        )
    try:
        result = run_command(
            [ctest_executable(), "--test-dir", str(build_dir), "-N"],
            timeout=30,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("CTest discovery timed out after 30 seconds.") from error
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "Could not discover CTest cases.")
    tests = [match.group(1) for line in result.stdout.splitlines() if (match := TEST_LINE.match(line))]
    if not tests:
        raise RuntimeError("CTest discovered no tests. Build the project with BUILD_TESTING enabled.")
    return tests


def run_tests(build_dir, selected_tests):
    ctest = ctest_executable()
    results = []
    for test_name in selected_tests:
        started = time.perf_counter()
        output = ""
        status = "Failed"
        try:
            result = run_command(
                [
                    ctest,
                    "--test-dir",
                    str(build_dir),
                    "--output-on-failure",
                    "-R",
                    f"^{re.escape(test_name)}$",
                ],
                timeout=180,
            )
            output = (result.stdout + result.stderr).strip()
            status = "Passed" if result.returncode == 0 else "Failed"
        except subprocess.TimeoutExpired as error:
            output = f"Test timed out after 180 seconds.\n{error}"
        except OSError as error:
            output = str(error)
        results.append(
            {
                "Test": test_name,
                "Result": status,
                "Duration (s)": round(time.perf_counter() - started, 3),
                "Output": output,
            }
        )
    return results


with st.sidebar:
    st.header("Project setup")
    build_dir_text = st.text_input("CMake build directory", str(DEFAULT_BUILD_DIR))
    build_dir = Path(build_dir_text).expanduser()
    if not build_dir.is_absolute():
        build_dir = ROOT / build_dir
    st.caption("Build and run this POSIX shared-memory project in Linux or WSL.")
    if st.button("Initialize cache", use_container_width=True):
        try:
            response = call_bridge(build_dir, "init")
            st.session_state["cache_notice"] = response["message"]
            st.session_state.pop("cache_error", None)
            st.session_state.pop("cache_cleanup_notice", None)
        except (FileNotFoundError, RuntimeError) as error:
            st.session_state["cache_error"] = str(error)
    if st.session_state.get("cache_error"):
        st.error(st.session_state["cache_error"])
    elif st.session_state.get("cache_notice"):
        st.success(st.session_state["cache_notice"])
    if st.session_state.get("cache_cleanup_error"):
        st.error(st.session_state["cache_cleanup_error"])
    elif st.session_state.get("cache_cleanup_notice"):
        st.info(st.session_state["cache_cleanup_notice"])

    with st.expander("Danger zone: destroy shared-memory cache"):
        st.warning(
            "Stop the writer, reader, demo, and any other cache clients before cleanup. "
            "Unlinking does not close mappings that are already open; if a client remains "
            "connected while another process initializes a cache, they may use different cache objects."
        )
        clients_stopped = st.checkbox(
            "I have stopped all cache clients.",
            key="confirm_cache_clients_stopped",
        )
        confirmation = st.text_input(
            "Type UNLINK to confirm",
            key="confirm_cache_unlink_text",
        )
        if st.button(
            "Destroy shared-memory cache",
            disabled=not (clients_stopped and confirmation == "UNLINK"),
            use_container_width=True,
        ):
            try:
                st.session_state["cache_cleanup_notice"] = cleanup_cache(build_dir)
                st.session_state.pop("cache_cleanup_error", None)
                st.session_state.pop("cache_notice", None)
                st.session_state.pop("cache_error", None)
                st.info(st.session_state["cache_cleanup_notice"])
            except (FileNotFoundError, RuntimeError, OSError) as error:
                st.session_state["cache_cleanup_error"] = str(error)
                st.session_state.pop("cache_cleanup_notice", None)
                st.error(st.session_state["cache_cleanup_error"])


cache_tab, tests_tab = st.tabs(["Cache Explorer", "Test Lab"])

with cache_tab:
    st.subheader("Shared-memory key-value cache")
    try:
        cache_data = call_bridge(build_dir, "list")
        cache_items = cache_data["items"]
        stats = cache_data["stats"]

        metric_columns = st.columns(5)
        metric_columns[0].metric("Allocated entries", stats["entries"])
        metric_columns[1].metric("Capacity", stats["capacity"])
        metric_columns[2].metric("Hits", stats["hits"])
        metric_columns[3].metric("Misses", stats["misses"])
        metric_columns[4].metric("Collisions", stats["collisions"])

        st.markdown("#### Records")
        if cache_items:
            st.dataframe(
                [
                    {
                        "Key": item["key"],
                        "Value": item["value"],
                        "Last inserted/updated (UTC)": time.strftime(
                            "%Y-%m-%d %H:%M:%S", time.gmtime(item["timestamp"])
                        ),
                        "TTL (seconds)": item["ttl_seconds"] or "Never expires",
                        "TTL status": "Expired (lazy eviction)" if item["expired"] else "Available",
                    }
                    for item in cache_items
                ],
                use_container_width=True,
                hide_index=True,
            )
        else:
            st.info("The cache is empty. Create a record below to get started.")

        st.markdown("#### CRUD operations")
        if st.session_state.get("operation_notice"):
            st.success(st.session_state.pop("operation_notice"))
        create_tab, read_tab, update_tab, delete_tab = st.tabs(
            ["Create", "Read", "Update", "Delete"]
        )
        with create_tab:
            with st.form("put_form"):
                key = st.text_input("Key", max_chars=63, key="put_key")
                value = st.text_area("Value", max_chars=255, key="put_value")
                ttl = st.number_input("TTL in seconds (0 = no expiry)", min_value=0, step=1)
                submitted = st.form_submit_button("Create", type="primary")
            if submitted:
                if not key:
                    st.error("Enter a key.")
                else:
                    try:
                        call_bridge(build_dir, "put", key, value, int(ttl))
                        st.session_state["operation_notice"] = f"Stored {key!r}."
                        st.rerun()
                    except (FileNotFoundError, RuntimeError) as error:
                        st.error(str(error))

        with read_tab:
            with st.form("get_form"):
                key = st.text_input("Key to read", max_chars=63, key="get_key")
                submitted = st.form_submit_button("Read key")
            if submitted:
                if not key:
                    st.error("Enter a key.")
                else:
                    try:
                        response = call_bridge(build_dir, "get", key)
                        if response["found"]:
                            st.success(f"Value: {response['value']}")
                            st.caption(f"Last inserted/updated: {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(response['timestamp']))}")
                        else:
                            st.warning("Cache miss: key not found or expired.")
                    except (FileNotFoundError, RuntimeError) as error:
                        st.error(str(error))

        with update_tab:
            with st.form("update_form"):
                key = st.text_input("Key to update", max_chars=63, key="update_key")
                value = st.text_area("New value", max_chars=255, key="update_value")
                submitted = st.form_submit_button("Update existing key")
            if submitted:
                if not key:
                    st.error("Enter a key.")
                else:
                    try:
                        call_bridge(build_dir, "update", key, value)
                        st.session_state["operation_notice"] = f"Updated {key!r}."
                        st.rerun()
                    except (FileNotFoundError, RuntimeError) as error:
                        st.error(str(error))

        with delete_tab:
            with st.form("delete_form"):
                key = st.text_input("Key to delete", max_chars=63, key="delete_key")
                submitted = st.form_submit_button("Delete key")
            if submitted:
                if not key:
                    st.error("Enter a key.")
                else:
                    try:
                        call_bridge(build_dir, "delete", key)
                        st.session_state["operation_notice"] = f"Deleted {key!r}."
                        st.rerun()
                    except (FileNotFoundError, RuntimeError) as error:
                        st.error(str(error))
    except (FileNotFoundError, RuntimeError, KeyError) as error:
        st.warning(f"Cache unavailable: {error}")
        st.info("Build the project, then use **Initialize cache** in the sidebar.")

with tests_tab:
    st.subheader("GoogleTest / CTest runner")
    st.caption("Choose individual cases, a subset, or run the full discovered suite.")
    try:
        tests = discover_tests(build_dir)
        selected_tests = st.multiselect(
            f"Test cases ({len(tests)} discovered)",
            tests,
            key="selected_tests",
        )
        run_column, run_all_column = st.columns(2)
        run_selected = run_column.button(
            "Run selected tests",
            disabled=not selected_tests,
            use_container_width=True,
            type="primary",
        )
        run_all = run_all_column.button(
            "Run all tests",
            use_container_width=True,
        )

        if run_selected or run_all:
            names_to_run = tests if run_all else selected_tests
            with st.spinner(f"Running {len(names_to_run)} test(s)..."):
                st.session_state["test_results"] = run_tests(build_dir, names_to_run)

        results = st.session_state.get("test_results", [])
        if results:
            passed = sum(result["Result"] == "Passed" for result in results)
            failed = len(results) - passed
            duration = sum(result["Duration (s)"] for result in results)
            summary_columns = st.columns(3)
            summary_columns[0].metric("Passed", passed)
            summary_columns[1].metric("Failed", failed)
            summary_columns[2].metric("Total time", f"{duration:.3f} s")
            st.dataframe(
                [
                    {
                        "Test": result["Test"],
                        "Result": result["Result"],
                        "Duration (s)": result["Duration (s)"],
                    }
                    for result in results
                ],
                use_container_width=True,
                hide_index=True,
            )
            for result in results:
                if result["Result"] == "Failed":
                    with st.expander(f"Failure output: {result['Test']}", expanded=True):
                        st.code(result["Output"] or "CTest failed without output.")
    except (FileNotFoundError, RuntimeError) as error:
        st.warning(f"Tests unavailable: {error}")
        st.info("Configure and build with CMake and BUILD_TESTING enabled, then reload this page.")
