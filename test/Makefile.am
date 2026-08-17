TESTS = \
    run-out-of-tree-build.sh \
    run-complexvar-check.sh \
    run-ncols-guard-check.sh \
    run-derived-cache-check.sh \
    run-dataset-leak-check.sh \
    run-dataset-minmax-check.sh \
    run-copyvar-check.sh \
    run-derived-name-check.sh

TESTS_ENVIRONMENT =

check-local:
	python3 $(srcdir)/test_python_protocol.py
	python3 $(srcdir)/test_python_plot_protocol.py

check-smoke:
	$(srcdir)/run-complex-smoke-check.sh $(top_srcdir)

EXTRA_DIST = \
    run-out-of-tree-build.sh \
    run-complexvar-check.sh \
    run-ncols-guard-check.sh \
    run-derived-cache-check.sh \
    run-dataset-leak-check.sh \
    run-dataset-minmax-check.sh \
    run-copyvar-check.sh \
    run-derived-name-check.sh \
    run-complex-smoke-check.sh \
    test_complexvar_harness.c \
    test_copyvar_harness.c \
    test_dataset_leak_harness.c \
    test_dataset_minmax_harness.c \
    test_derived_cache_harness.c \
    test_derived_name_resolv_harness.c \
    test_ncols_guard_harness.c \
    test_python_protocol.py \
    test_python_plot_protocol.py \
    test_complex_smoke_broadway.js \
    complex_smoke.raw

MOSTLYCLEANFILES = *.log *~
CLEANFILES = *.log *~
DISTCLEANFILES = *.log *~
