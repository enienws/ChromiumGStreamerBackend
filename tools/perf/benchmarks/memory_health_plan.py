# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re

from core import perf_benchmark

from telemetry import benchmark
from telemetry.timeline import tracing_category_filter
from telemetry.web_perf import timeline_based_measurement

import page_sets


RE_BENCHMARK_VALUES = re.compile('(fore|back)ground-memory_')


@benchmark.Enabled('android')
class MemoryHealthPlan(perf_benchmark.PerfBenchmark):
  """Timeline based benchmark for the Memory Health Plan."""

  page_set = page_sets.MemoryHealthStory

  def CreateTimelineBasedMeasurementOptions(self):
    trace_memory = tracing_category_filter.TracingCategoryFilter(
      filter_string='disabled-by-default-memory-infra')
    return timeline_based_measurement.Options(overhead_level=trace_memory)

  @classmethod
  def Name(cls):
    return 'memory.memory_health_plan'

  @classmethod
  def ValueCanBeAddedPredicate(cls, value, is_first_result):
    return bool(RE_BENCHMARK_VALUES.match(value.name))
