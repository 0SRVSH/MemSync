from collections import defaultdict

import streamlit as st

from services import run_benchmark


WORKLOAD_DESCRIPTIONS = {
    "get_hit": "Read a value that is already in the cache.",
    "get_miss": "Look up a key that is not in the cache.",
    "put_update": "Alternate between adding and updating values.",
    "mixed": "Mix reads, missing-key lookups, adds, and updates.",
}


def _average_results(rows):
    grouped = defaultdict(list)
    for row in rows:
        key = (
            row["Implementation"],
            row["Workload"],
            row["Payload (bytes)"],
            row["Threads"],
        )
        grouped[key].append(row)

    averaged = []
    for (implementation, workload, payload, threads), trials in grouped.items():
        averaged.append(
            {
                "Implementation": implementation,
                "Workload": workload,
                "Payload (bytes)": payload,
                "Threads": threads,
                "Trials": len(trials),
                "Failures": sum(row["Failures"] for row in trials),
                "Average median latency (us)": sum(
                    row["Median latency (us)"] for row in trials
                )
                / len(trials),
                "Average p95 latency (us)": sum(
                    row["p95 latency (us)"] for row in trials
                )
                / len(trials),
                "Average p99 latency (us)": sum(
                    row["p99 latency (us)"] for row in trials
                )
                / len(trials),
                "Average throughput (ops/s)": sum(
                    row["Throughput (ops/s)"] for row in trials
                )
                / len(trials),
            }
        )
    return averaged


def render_benchmark_lab(build_dir):
    st.subheader("Performance benchmark")
    st.markdown(
        "This test compares MemSync's shared-memory cache with a local Unix-socket "
        "cache. In everyday terms, it compares sharing data directly through memory "
        "with sending each request through a local message channel."
    )
    st.caption(
        "The comparison is specific to this machine and benchmark setup; it is not "
        "a guarantee of performance in every application."
    )

    with st.expander("What do the test scenarios mean?"):
        for workload, description in WORKLOAD_DESCRIPTIONS.items():
            st.markdown(f"- **{workload.replace('_', ' ').title()}:** {description}")
        st.markdown(
            "- **Payload size:** how many bytes are stored in each test value.\n"
            "- **Threads:** how many workers make requests at the same time.\n"
            "- **Latency:** how long one request takes; lower is faster.\n"
            "- **Throughput:** how many requests complete per second; higher is faster.\n"
            "- **p95 / p99:** the request time that 95% / 99% of measured requests "
            "were at or below."
        )

    settings_column, action_column = st.columns([2, 1])
    operations = settings_column.number_input(
        "Operations per worker, per test",
        min_value=100,
        max_value=10000,
        value=2000,
        step=100,
        help="More operations can make the comparison steadier but take longer.",
    )
    trials = settings_column.number_input(
        "Trials",
        min_value=1,
        max_value=5,
        value=3,
        step=1,
        help="Results are averaged across repeated trials.",
    )
    run_clicked = action_column.button(
        "Run benchmark",
        type="primary",
        use_container_width=True,
        help="Runs all workload, payload-size, and thread-count combinations.",
    )
    if run_clicked:
        try:
            with st.spinner(
                "Running benchmark scenarios. This may take a little while..."
            ):
                st.session_state["benchmark_result"] = run_benchmark(
                    build_dir, int(operations), int(trials)
                )
            st.session_state.pop("benchmark_error", None)
        except (FileNotFoundError, RuntimeError) as error:
            st.session_state["benchmark_error"] = str(error)
            st.session_state.pop("benchmark_result", None)

    if st.session_state.get("benchmark_error"):
        st.error(st.session_state["benchmark_error"])

    benchmark_result = st.session_state.get("benchmark_result")
    if not benchmark_result:
        st.info("Run the benchmark to see a plain-language comparison.")
        return

    results = _average_results(benchmark_result["rows"])
    workloads = sorted({row["Workload"] for row in results})
    payload_sizes = sorted({row["Payload (bytes)"] for row in results})
    thread_counts = sorted({row["Threads"] for row in results})
    filter_columns = st.columns(3)
    selected_workload = filter_columns[0].selectbox(
        "Workload",
        workloads,
        format_func=lambda value: value.replace("_", " ").title(),
    )
    selected_payload = filter_columns[1].selectbox(
        "Value size",
        payload_sizes,
        format_func=lambda value: f"{value} bytes",
    )
    selected_threads = filter_columns[2].selectbox(
        "Workers",
        thread_counts,
        format_func=lambda value: f"{value} worker(s)",
    )
    comparison = [
        row
        for row in results
        if row["Workload"] == selected_workload
        and row["Payload (bytes)"] == selected_payload
        and row["Threads"] == selected_threads
    ]
    by_implementation = {
        row["Implementation"]: row for row in comparison
    }
    shared = by_implementation.get("shared_memory")
    socket = by_implementation.get("unix_socket")

    st.markdown(
        f"#### {selected_workload.replace('_', ' ').title()} · "
        f"{selected_payload}-byte values · {selected_threads} worker(s)"
    )
    if shared and socket:
        metric_columns = st.columns(3)
        metric_columns[0].metric(
            "Shared-memory average latency",
            f"{shared['Average median latency (us)']:.3g} us",
        )
        metric_columns[1].metric(
            "Unix-socket average latency",
            f"{socket['Average median latency (us)']:.3g} us",
        )
        metric_columns[2].metric(
            "Shared-memory throughput",
            f"{shared['Average throughput (ops/s)']:,.0f} ops/s",
        )
        if shared["Failures"] == 0 and socket["Failures"] == 0:
            st.success(
                "Both implementations completed the measured operations without "
                "reported failures."
            )
        else:
            st.warning(
                "One or more measured operations failed. Check the detailed results "
                "before interpreting the comparison."
            )

        shared_throughput = shared["Average throughput (ops/s)"]
        socket_throughput = socket["Average throughput (ops/s)"]
        if (
            shared["Failures"] == 0
            and socket["Failures"] == 0
            and shared_throughput > 0
            and socket_throughput > 0
        ):
            if shared_throughput >= socket_throughput:
                ratio = shared_throughput / socket_throughput
                faster_option = "shared memory"
            else:
                ratio = socket_throughput / shared_throughput
                faster_option = "the Unix-socket comparison"
            st.info(
                f"For these settings, {faster_option} handled about "
                f"{ratio:.1f} times as many requests per second. This describes "
                "this benchmark run on this machine, not every application."
            )

        st.bar_chart(
            [
                {
                    "Implementation": "Shared memory",
                    "Throughput (ops/s)": shared["Average throughput (ops/s)"],
                },
                {
                    "Implementation": "Unix socket",
                    "Throughput (ops/s)": socket["Average throughput (ops/s)"],
                },
            ],
            x="Implementation",
            y="Throughput (ops/s)",
            use_container_width=True,
        )

    st.caption(
        "Numbers above average each trial's measured median and throughput. "
        "The p95/p99 columns below describe slower requests, which averages can hide."
    )
    st.dataframe(
        [
            {
                "Implementation": (
                    "Shared memory"
                    if row["Implementation"] == "shared_memory"
                    else "Unix socket"
                ),
                "Trials": row["Trials"],
                "Failures": row["Failures"],
                "Average median latency (us)": round(
                    row["Average median latency (us)"], 3
                ),
                "Average p95 latency (us)": round(
                    row["Average p95 latency (us)"], 3
                ),
                "Average p99 latency (us)": round(
                    row["Average p99 latency (us)"], 3
                ),
                "Average throughput (ops/s)": round(
                    row["Average throughput (ops/s)"], 1
                ),
            }
            for row in comparison
        ],
        use_container_width=True,
        hide_index=True,
    )

    with st.expander("All workload results"):
        st.dataframe(
            [
                {
                    "Implementation": (
                        "Shared memory"
                        if row["Implementation"] == "shared_memory"
                        else "Unix socket"
                    ),
                    "Workload": row["Workload"].replace("_", " ").title(),
                    "Payload (bytes)": row["Payload (bytes)"],
                    "Threads": row["Threads"],
                    "Trials": row["Trials"],
                    "Failures": row["Failures"],
                    "Average median latency (us)": round(
                        row["Average median latency (us)"], 3
                    ),
                    "Average p95 latency (us)": round(
                        row["Average p95 latency (us)"], 3
                    ),
                    "Average p99 latency (us)": round(
                        row["Average p99 latency (us)"], 3
                    ),
                    "Average throughput (ops/s)": round(
                        row["Average throughput (ops/s)"], 1
                    ),
                }
                for row in results
            ],
            use_container_width=True,
            hide_index=True,
        )
    if benchmark_result["summary"]:
        with st.expander("Machine and benchmark details"):
            st.code(benchmark_result["summary"])
