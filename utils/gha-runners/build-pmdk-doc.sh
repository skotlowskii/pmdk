#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2023, Intel Corporation

#
# build-pmdk.sh - Script for building pmdk project
#

FULL_PATH=$(readlink -f $(dirname ${BASH_SOURCE[0]}))
PMDK_PATH="${FULL_PATH}/../.."
DOC_ARGUMENTS="PMEM2_INSTALL=y PMEMSET_INSTALL=y doc install prefix=${PMDK_PATH}/../install -j"

set -eo pipefail

#
# build_pmdk_documentation -- build pmdk documentation from source
#
function build_pmdk_documentation {
	echo "********** make pmdk_documentation **********"
	cd ${PMDK_PATH} && make ${DOC_ARGUMENTS}
}

build_pmdk_documentation
