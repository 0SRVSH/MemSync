import streamlit as st

from benchmark_view import render_benchmark_lab
from cache_view import render_cache_explorer
from project_sidebar import render_project_sidebar
from test_view import render_test_lab


st.set_page_config(page_title="MemSync Console", page_icon="🧠", layout="wide")
st.title("MemSync Console")
st.caption("Explore the shared-memory cache, run tests, and compare performance.")

build_dir = render_project_sidebar()
cache_tab, tests_tab, benchmark_tab = st.tabs(
    ["Cache Explorer", "Test Lab", "Benchmark"]
)

with cache_tab:
    render_cache_explorer(build_dir)

with tests_tab:
    render_test_lab(build_dir)

with benchmark_tab:
    render_benchmark_lab(build_dir)
