#!../env.py
#
# Copyright 2019, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#


import testframework as t
import futils as f


@t.require_granularity(t.ANY)
class PMEM2_CONFIG(t.BaseTest):
    test_type = t.Short


class TEST0(PMEM2_CONFIG):
    """test granularity detection with PMEM2_FORCE_GRANULARITY set to page"""
    def run(self, ctx):
        ctx.env['PMEM2_FORCE_GRANULARITY'] = "page"
        ctx.exec(f.get_test_tool_path(ctx.build, "gran_detecto"),
                 '-p', ctx.testdir)


class TEST1(PMEM2_CONFIG):
    """test granularity detection with PMEM2_FORCE_GRANULARITY
    set to cache_line"""
    def run(self, ctx):
        ctx.env['PMEM2_FORCE_GRANULARITY'] = "cache_line"
        ctx.exec(f.get_test_tool_path(ctx.build, "gran_detecto"),
                 '-c', ctx.testdir)


class TEST2(PMEM2_CONFIG):
    """test granularity detection with PMEM2_FORCE_GRANULARITY set to byte"""
    def run(self, ctx):
        ctx.env['PMEM2_FORCE_GRANULARITY'] = "byte"
        ctx.exec(f.get_test_tool_path(ctx.build, "gran_detecto"),
                 '-b', ctx.testdir)


class TEST3(PMEM2_CONFIG):
    """test granularity detection with PMEM2_FORCE_GRANULARITY
    set to CaCHe_Line"""
    def run(self, ctx):
        ctx.env['PMEM2_FORCE_GRANULARITY'] = "CaCHe_Line"
        ctx.exec(f.get_test_tool_path(ctx.build, "gran_detecto"),
                 '-c', ctx.testdir)


class TEST4(PMEM2_CONFIG):
    """test granularity detection with PMEM2_FORCE_GRANULARITY
    set to CACHELINE"""
    def run(self, ctx):
        ctx.env['PMEM2_FORCE_GRANULARITY'] = "CACHELINE"
        ctx.exec(f.get_test_tool_path(ctx.build, "gran_detecto"),
                 '-c', ctx.testdir)
