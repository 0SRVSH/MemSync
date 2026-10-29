import time

import streamlit as st

from services import call_bridge


def render_cache_explorer(build_dir):
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
                            st.caption(
                                "Last inserted/updated: "
                                f"{time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(response['timestamp']))}"
                            )
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
