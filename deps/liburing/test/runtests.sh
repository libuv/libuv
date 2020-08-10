#!/bin/bash

TESTS="$@"
RET=0

TIMEOUT=60
FAILED=""
MAYBE_FAILED=""

do_kmsg="1"
if ! [ $(id -u) = 0 ]; then
	do_kmsg="0"
fi

TEST_DIR=$(dirname $0)
TEST_FILES=""
if [ -f "$TEST_DIR/config.local" ]; then
	. $TEST_DIR/config.local
	for dev in $TEST_FILES; do
		if [ ! -e "$dev" ]; then
			echo "Test file $dev not valid"
			exit 1
		fi
	done
fi

_check_dmesg()
{
	local dmesg_marker="$1"
	local seqres="$2.seqres"

	if [[ $do_kmsg -eq 0 ]]; then
		return 0
	fi

	dmesg | bash -c "$DMESG_FILTER" | grep -A 9999 "$dmesg_marker" >"${seqres}.dmesg"
	grep -q -e "kernel BUG at" \
	     -e "WARNING:" \
	     -e "BUG:" \
	     -e "Oops:" \
	     -e "possible recursive locking detected" \
	     -e "Internal error" \
	     -e "INFO: suspicious RCU usage" \
	     -e "INFO: possible circular locking dependency detected" \
	     -e "general protection fault:" \
	     -e "blktests failure" \
	     "${seqres}.dmesg"
	# shellcheck disable=SC2181
	if [[ $? -eq 0 ]]; then
		return 1
	else
		rm -f "${seqres}.dmesg"
		return 0
	fi
}

run_test()
{
	T="$1"
	D="$2"
	DMESG_FILTER="cat"

	if [ "$do_kmsg" -eq 1 ]; then
		if [ -z "$D" ]; then
			local dmesg_marker="Running test $T:"
		else
			local dmesg_marker="Running test $T $D:"
		fi
		echo $dmesg_marker | tee /dev/kmsg
	else
		local dmesg_marker=""
		echo Running test $T $D
	fi
	timeout --preserve-status -s INT $TIMEOUT ./$T $D
	r=$?
	if [ "${r}" -eq 124 ]; then
		echo "Test $T timed out (may not be a failure)"
	elif [ "${r}" -ne 0 ]; then
		echo "Test $T failed with ret ${r}"
		if [ -z "$D" ]; then
			FAILED="$FAILED <$T>"
		else
			FAILED="$FAILED <$T $D>"
		fi
		RET=1
	elif ! _check_dmesg "$dmesg_marker" "$T"; then
		echo "Test $T failed dmesg check"
		if [ -z "$D" ]; then
			FAILED="$FAILED <$T>"
		else
			FAILED="$FAILED <$T $D>"
		fi
		RET=1
	elif [ ! -z "$D" ]; then
		sleep .1
		ps aux | grep "\[io_wq_manager\]" > /dev/null
		R="$?"
		if [ "$R" -eq 0 ]; then
			MAYBE_FAILED="$MAYBE_FAILED $T"
		fi
	fi
}

for t in $TESTS; do
	run_test $t
	if [ ! -z "$TEST_FILES" ]; then
		for dev in $TEST_FILES; do
			run_test $t $dev
		done
	fi
done

if [ "${RET}" -ne 0 ]; then
	echo "Tests $FAILED failed"
	exit $RET
else
	sleep 1
	ps aux | grep "\[io_wq_manager\]" > /dev/null
	R="$?"
	if [ "$R" -ne 0 ]; then
		MAYBE_FAILED=""
	fi
	if [ ! -z "$MAYBE_FAILED" ]; then
		echo "Tests _maybe_ failed: $MAYBE_FAILED"
	fi
	echo "All tests passed"
	exit 0
fi
