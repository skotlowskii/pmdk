#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2022-2023, Intel Corporation

#
# build-pmdk.sh - Scripts for building pmdk project
#

FULL_PATH=$(readlink -f $(dirname ${BASH_SOURCE[0]}))
PMDK_PATH="${FULL_PATH}/../.."
DOC_ARGUMENTS="PMEM2_INSTALL=y doc install prefix=${PMDK_PATH}/../install -j"

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

#
# build_documentation -- build documentation from source
#
function build_documentation {
	echo "********** make pmdk_documentation **********"
	cd ${PMDK_PATH} && make ${DOC_ARGUMENTS}
}

if [ "$#" -eq 1 ]; then
  build_method=$1
  ${build_method}
fi
