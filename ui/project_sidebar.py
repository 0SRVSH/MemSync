from pathlib import Path

import streamlit as st

from services import DEFAULT_BUILD_DIR, cleanup_cache, call_bridge


def render_project_sidebar():
    with st.sidebar:
        st.header("Project setup")
        build_dir_text = st.text_input("CMake build directory", str(DEFAULT_BUILD_DIR))
        build_dir = Path(build_dir_text).expanduser()
        if not build_dir.is_absolute():
            build_dir = DEFAULT_BUILD_DIR.parent / build_dir
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
    return build_dir
