import streamlit as st

from services import discover_tests, run_tests


def render_test_lab(build_dir):
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
