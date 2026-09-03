<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useRouter } from "vue-router";
import { getDashboard, type DashboardData } from "../backend";
import SignalBadge from "../components/SignalBadge.vue";
import { displayFunction, formatRelative, formatTimestamp } from "../utils";

const router = useRouter();
const loading = ref(true);
const error = ref<string | null>(null);
const data = ref<DashboardData | null>(null);

const trendMax = computed(() =>
  Math.max(1, ...(data.value?.trend.map((p) => p.count) ?? [1])),
);

async function load() {
  loading.value = true;
  error.value = null;
  try {
    data.value = await getDashboard();
  } catch (err) {
    error.value =
      err instanceof Error ? err.message : "Unable to load dashboard from the local database.";
  } finally {
    loading.value = false;
  }
}

onMounted(load);

function openGroup(id: number) {
  router.push(`/crashes/${id}`);
}
</script>

<template>
  <div class="page">
    <h1 class="page-title">Dashboard</h1>

    <div v-if="error" class="error-banner">{{ error }}</div>
    <div v-if="loading" class="loading">Loading dashboard…</div>

    <template v-else-if="data">
      <section class="metric-grid">
        <div class="metric-card">
          <div class="metric-label">Crashes Today</div>
          <div class="metric-value">{{ data.summary.crashes_today }}</div>
        </div>
        <div class="metric-card">
          <div class="metric-label">Unique Groups</div>
          <div class="metric-value">{{ data.summary.unique_groups }}</div>
        </div>
        <div class="metric-card">
          <div class="metric-label">Total Occurrences</div>
          <div class="metric-value">{{ data.summary.total_occurrences }}</div>
        </div>
        <div class="metric-card">
          <div class="metric-label">Affected Versions</div>
          <div class="metric-value">{{ data.summary.affected_versions }}</div>
        </div>
      </section>

      <div class="grid-2">
        <section class="panel">
          <div class="panel-header">Most Frequent Crashes</div>
          <div class="panel-body table-wrap">
            <table v-if="data.frequent_crashes.length" class="data-table">
              <thead>
                <tr>
                  <th>Signal</th>
                  <th>Function</th>
                  <th>Occurrences</th>
                  <th>Last Seen</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="row in data.frequent_crashes"
                  :key="row.id"
                  class="clickable"
                  @click="openGroup(row.id)"
                >
                  <td><SignalBadge :signal="row.signal" /></td>
                  <td class="mono">{{ displayFunction(row.top_function) }}</td>
                  <td>{{ row.occurrence_count }}</td>
                  <td>{{ formatRelative(row.last_seen) }}</td>
                </tr>
              </tbody>
            </table>
            <div v-else class="empty-state">
              <h3>No crash groups yet</h3>
              <p>Integrate the CrashVault SDK and import crash reports to populate the dashboard.</p>
            </div>
          </div>
        </section>

        <section class="panel">
          <div class="panel-header">Occurrence Trend (14 days)</div>
          <div class="panel-body">
            <div v-if="!data.trend.length" class="empty-trend">No trend data yet</div>
            <div v-else class="trend-chart" role="img" aria-label="Crash occurrence trend">
              <div v-for="point in data.trend" :key="point.day" class="trend-bar-wrap">
                <div
                  class="trend-bar"
                  :style="{ height: `${Math.max(4, (point.count / trendMax) * 100)}%` }"
                  :title="`${point.day}: ${point.count}`"
                />
                <div class="trend-label">{{ point.day.slice(5) }}</div>
              </div>
            </div>
          </div>
        </section>
      </div>

      <section class="panel">
        <div class="panel-header">Recent Activity</div>
        <div class="panel-body table-wrap">
          <table v-if="data.recent_activity.length" class="data-table">
            <thead>
              <tr>
                <th>Signal</th>
                <th>Function</th>
                <th>Application</th>
                <th>Imported</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="row in data.recent_activity"
                :key="row.occurrence_id"
                class="clickable"
                @click="openGroup(row.group_id)"
              >
                <td><SignalBadge :signal="row.signal" /></td>
                <td class="mono">{{ displayFunction(row.top_function) }}</td>
                <td>{{ row.application }}</td>
                <td>{{ formatTimestamp(row.imported_at) }}</td>
              </tr>
            </tbody>
          </table>
          <div v-else class="empty-state">
            <p>No recent crash activity.</p>
          </div>
        </div>
      </section>
    </template>
  </div>
</template>

<style scoped>
.page-title {
  margin: 0 0 var(--space-4);
  font-size: 18px;
  font-weight: 600;
}

.grid-2 {
  display: grid;
  grid-template-columns: 1.2fr 0.8fr;
  gap: var(--space-4);
  margin: var(--space-4) 0;
}

.empty-trend {
  color: var(--text-muted);
  font-size: 12px;
  padding: var(--space-4) 0;
}

@media (max-width: 1100px) {
  .grid-2 {
    grid-template-columns: 1fr;
  }
}
</style>
