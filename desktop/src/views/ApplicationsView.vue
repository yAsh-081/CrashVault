<script setup lang="ts">
import { onMounted, ref } from "vue";
import { useRouter } from "vue-router";
import { getApplications, type ApplicationSummary } from "../backend";
import { formatRelative } from "../utils";

const router = useRouter();
const loading = ref(true);
const error = ref<string | null>(null);
const apps = ref<ApplicationSummary[]>([]);

async function load() {
  loading.value = true;
  error.value = null;
  try {
    apps.value = await getApplications();
  } catch (err) {
    error.value =
      err instanceof Error
        ? err.message
        : "Unable to load applications from the local database.";
  } finally {
    loading.value = false;
  }
}

onMounted(load);

function openApp(name: string) {
  router.push({ path: "/crashes", query: { app: name } });
}
</script>

<template>
  <div class="page">
    <h1 class="page-title">Applications</h1>

    <div v-if="error" class="error-banner">{{ error }}</div>
    <div v-if="loading" class="loading">Loading applications…</div>

    <section v-else class="panel">
      <div class="panel-body table-wrap">
        <table v-if="apps.length" class="data-table">
          <thead>
            <tr>
              <th>Application</th>
              <th>Groups</th>
              <th>Occurrences</th>
              <th>Last Crash</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="app in apps" :key="app.name" class="clickable" @click="openApp(app.name)">
              <td>{{ app.name }}</td>
              <td>{{ app.group_count }}</td>
              <td>{{ app.occurrence_count }}</td>
              <td>{{ formatRelative(app.last_crash) }}</td>
            </tr>
          </tbody>
        </table>
        <div v-else class="empty-state">
          <h3>No applications yet</h3>
          <p>Applications are created automatically when crash reports are imported.</p>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.page-title {
  margin: 0 0 var(--space-4);
  font-size: 18px;
  font-weight: 600;
}
</style>
