#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2022-2023, Intel Corporation

#
# build-pmdk.sh - Scripts for building pmdk project
#

FULL_PATH=$(readlink -f $(dirname ${BASH_SOURCE[0]}))
PMDK_PATH="${FULL_PATH}/../.."

set -eo pipefail

#
# build_and_test -- build pmdk from source
#
function build_and_test {
	echo "********** make pmdk **********"
	cd ${PMDK_PATH} && make clean
	cd ${PMDK_PATH} && make EXTRA_CFLAGS=-DUSE_VALGRIND
	echo "********** make pmdk test **********"
	cd ${PMDK_PATH}/ && make test
}

build_and_test
