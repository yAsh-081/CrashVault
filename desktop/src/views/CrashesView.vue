<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import { useRoute, useRouter } from "vue-router";
import { getApplications, getCrashGroups, type CrashGroupSummary } from "../backend";
import FilterSelect from "../components/FilterSelect.vue";
import PlainTextField from "../components/PlainTextField.vue";
import SignalBadge from "../components/SignalBadge.vue";
import { displayFunction, formatRelative, formatTimestamp } from "../utils";

const router = useRouter();
const route = useRoute();

const loading = ref(true);
const error = ref<string | null>(null);
const groups = ref<CrashGroupSummary[]>([]);
const applications = ref<string[]>([]);

const applicationFilter = ref("");
const signalFilter = ref("");
const search = ref("");

const applicationOptions = computed(() => [
  { value: "", label: "All Applications" },
  ...applications.value.map((name) => ({ value: name, label: name })),
]);

const signalOptions = [
  { value: "", label: "All Signals" },
  { value: "11", label: "SIGSEGV" },
  { value: "6", label: "SIGABRT" },
  { value: "8", label: "SIGFPE" },
  { value: "4", label: "SIGILL" },
];

async function load() {
  loading.value = true;
  error.value = null;
  try {
    const [rows, apps] = await Promise.all([
      getCrashGroups({
        application: applicationFilter.value || null,
        signal: signalFilter.value ? Number(signalFilter.value) : null,
        search: search.value || null,
      }),
      getApplications(),
    ]);
    groups.value = rows;
    applications.value = apps.map((a) => a.name);
  } catch (err) {
    error.value = err instanceof Error ? err.message : "Failed to load crash groups";
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  if (typeof route.query.app === "string") {
    applicationFilter.value = route.query.app;
  }
  load();
});

watch([applicationFilter, signalFilter, search], () => {
  load();
});

function openGroup(id: number) {
  router.push(`/crashes/${id}`);
}
</script>

<template>
  <div class="page">
    <div class="page-head">
      <h1 class="page-title">Crashes</h1>
      <div class="filters">
        <FilterSelect
          v-model="applicationFilter"
          :options="applicationOptions"
          class="filter filter-application"
        />
        <FilterSelect v-model="signalFilter" :options="signalOptions" class="filter" />
        <PlainTextField v-model="search" placeholder="Search function or app" class="filter search" />
      </div>
    </div>

    <div v-if="error" class="error-banner">{{ error }}</div>
    <div v-if="loading" class="loading">Loading crashes…</div>

    <section v-else class="panel">
      <div class="panel-body table-wrap">
        <table v-if="groups.length" class="data-table">
          <thead>
            <tr>
              <th>Signal</th>
              <th>Top Function</th>
              <th>Application</th>
              <th>Versions</th>
              <th>Occurrences</th>
              <th>First Seen</th>
              <th>Last Seen</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="row in groups"
              :key="row.id"
              class="clickable"
              @click="openGroup(row.id)"
            >
              <td><SignalBadge :signal="row.signal" /></td>
              <td class="mono">{{ displayFunction(row.top_function) }}</td>
              <td>{{ row.application }}</td>
              <td class="versions">{{ row.versions.join(", ") || "—" }}</td>
              <td>{{ row.occurrence_count }}</td>
              <td>{{ formatTimestamp(row.first_seen) }}</td>
              <td>{{ formatRelative(row.last_seen) }}</td>
            </tr>
          </tbody>
        </table>
        <div v-else class="empty-state">
          <h3>No matching crash groups</h3>
          <p>Try adjusting filters or import new crash reports.</p>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.page-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: var(--space-4);
  margin-bottom: var(--space-4);
}

.page-title {
  margin: 0;
  font-size: 18px;
  font-weight: 600;
}

.filters {
  display: flex;
  gap: var(--space-2);
  flex-wrap: wrap;
  align-items: center;
}

.filter {
  min-width: 140px;
}

/* Application names like CrashVaultManualTest / CrashVaultDiagnosticTest */
.filter-application {
  min-width: 240px;
  flex: 1 1 240px;
  max-width: 280px;
}

.search {
  min-width: 220px;
}

.versions {
  max-width: 180px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
</style>
