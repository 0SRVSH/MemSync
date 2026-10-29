import csv
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD_DIR = ROOT / "build"
TEST_LINE = re.compile(r"^\s*Test\s+#\d+:\s+(.+?)\s*$")


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


def benchmark_path(build_dir):
    executable = "memsync_benchmark.exe" if sys.platform == "win32" else "memsync_benchmark"
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


def run_benchmark(build_dir, operations_per_worker, trials):
    executable = benchmark_path(build_dir)
    if not executable.is_file():
        raise FileNotFoundError(
            f"Benchmark executable not found at {executable}. Configure and build the project first."
        )
    try:
        result = run_command(
            [
                str(executable),
                "--operations",
                str(operations_per_worker),
                "--trials",
                str(trials),
            ],
            timeout=300,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Benchmark timed out after 300 seconds.") from error
    except OSError as error:
        raise RuntimeError(f"Could not run the benchmark: {error}") from error
    if result.returncode != 0:
        raise RuntimeError(
            result.stderr.strip() or result.stdout.strip() or "Benchmark failed."
        )

    reader = csv.DictReader(result.stdout.splitlines())
    required_fields = {
        "implementation",
        "workload",
        "payload_bytes",
        "threads",
        "trial",
        "operations",
        "failures",
        "median_us",
        "p95_us",
        "p99_us",
        "throughput_ops_sec",
    }
    if not reader.fieldnames or not required_fields.issubset(reader.fieldnames):
        raise RuntimeError("Benchmark returned CSV with an unexpected format.")

    rows = []
    try:
        for row in reader:
            rows.append(
                {
                    "Implementation": row["implementation"],
                    "Workload": row["workload"],
                    "Payload (bytes)": int(row["payload_bytes"]),
                    "Threads": int(row["threads"]),
                    "Trial": int(row["trial"]),
                    "Operations": int(row["operations"]),
                    "Failures": int(row["failures"]),
                    "Median latency (us)": float(row["median_us"]),
                    "p95 latency (us)": float(row["p95_us"]),
                    "p99 latency (us)": float(row["p99_us"]),
                    "Throughput (ops/s)": float(row["throughput_ops_sec"]),
                }
            )
    except (KeyError, TypeError, ValueError) as error:
        raise RuntimeError(f"Benchmark returned an invalid CSV row: {error}") from error
    if not rows:
        raise RuntimeError("Benchmark completed without producing any results.")
    return {"rows": rows, "summary": result.stderr.strip()}
