<script setup lang="ts">
import { onMounted, ref } from "vue";
import { open } from "@tauri-apps/plugin-dialog";
import {
  getSettings,
  getStorageStatus,
  processPendingReports,
  setSettings,
  type AppSettings,
  type StorageStatus,
} from "../backend";
import PlainTextField from "../components/PlainTextField.vue";

const loading = ref(true);
const saving = ref(false);
const error = ref<string | null>(null);
const message = ref<string | null>(null);
const settings = ref<AppSettings | null>(null);
const storage = ref<StorageStatus | null>(null);

async function load() {
  loading.value = true;
  error.value = null;
  try {
    settings.value = await getSettings();
    storage.value = await getStorageStatus();
  } catch (err) {
    error.value =
      err instanceof Error
        ? err.message
        : "Unable to load settings. Check that CrashVault can access its data directory.";
  } finally {
    loading.value = false;
  }
}

onMounted(load);

async function chooseDirectory() {
  if (!settings.value) {
    return;
  }
  const selected = await open({ directory: true, multiple: false });
  if (typeof selected === "string") {
    settings.value.crash_directory = selected;
  }
}

async function save() {
  if (!settings.value) {
    return;
  }
  saving.value = true;
  error.value = null;
  message.value = null;
  try {
    await setSettings(settings.value);
    storage.value = await getStorageStatus();
    message.value = "Settings saved.";
  } catch (err) {
    error.value = err instanceof Error ? err.message : "Failed to save settings.";
  } finally {
    saving.value = false;
  }
}

async function importNow() {
  error.value = null;
  message.value = null;
  try {
    const stats = await processPendingReports();
    storage.value = await getStorageStatus();
    message.value = `Imported ${stats.imported}, rejected ${stats.rejected}, duplicate ${stats.duplicate}.`;
  } catch (err) {
    error.value = err instanceof Error ? err.message : "Import failed.";
  }
}
</script>

<template>
  <div class="page">
    <h1 class="page-title">Settings</h1>

    <div v-if="error" class="error-banner">{{ error }}</div>
    <div v-if="message" class="message-banner">{{ message }}</div>
    <div v-if="loading" class="loading">Loading settings…</div>

    <template v-else-if="settings">
      <section class="panel">
        <div class="panel-header">Crash Reports Directory</div>
        <div class="panel-body settings-form">
          <label class="field">
            <span>Directory</span>
            <div class="dir-row">
              <PlainTextField v-model="settings.crash_directory" mono class="grow" />
              <span class="btn" @click="chooseDirectory">Choose Folder</span>
            </div>
          </label>

          <label class="toggle" @click.prevent="settings.auto_import = !settings.auto_import">
            <span class="toggle-box" :class="{ on: settings.auto_import }"></span>
            <span>Automatically import new reports</span>
          </label>

          <div class="actions">
            <span class="btn btn-primary" :class="{ disabled: saving }" @click="!saving && save()">Save Settings</span>
            <span class="btn" @click="importNow">Process Pending Reports</span>
          </div>
        </div>
      </section>

      <section v-if="storage" class="panel">
        <div class="panel-header">Storage</div>
        <div class="panel-body status-grid">
          <div>
            <span class="label">Database</span>
            <div class="mono">{{ storage.database_path }}</div>
          </div>
          <div>
            <span class="label">Pending reports</span>
            <div>{{ storage.pending_reports }}</div>
          </div>
          <div>
            <span class="label">Processed reports</span>
            <div>{{ storage.processed_reports }}</div>
          </div>
          <div>
            <span class="label">Database exists</span>
            <div>{{ storage.database_exists ? "Yes" : "No" }}</div>
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

.settings-form {
  display: flex;
  flex-direction: column;
  gap: var(--space-4);
}

.field span {
  display: block;
  margin-bottom: 6px;
  color: var(--text-secondary);
}

.dir-row {
  display: flex;
  gap: var(--space-2);
  align-items: stretch;
}

.dir-row .grow {
  flex: 1;
  min-width: 0;
}

.toggle {
  display: flex;
  align-items: center;
  gap: var(--space-2);
  color: var(--text-secondary);
  cursor: pointer;
  user-select: none;
}

.toggle-box {
  width: 16px;
  height: 16px;
  border-radius: 3px;
  background-color: #1e2229;
  border: 1px solid #2d333d;
  flex-shrink: 0;
}

.toggle-box.on {
  background-color: #3a6fc7;
  border-color: #4c8bf5;
  box-shadow: inset 0 0 0 2px #121418;
}

.actions {
  display: flex;
  gap: var(--space-2);
  flex-wrap: wrap;
}

.status-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: var(--space-3);
}

.label {
  display: block;
  color: var(--text-muted);
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  margin-bottom: 4px;
}

.message-banner {
  padding: var(--space-3) var(--space-4);
  border: 1px solid #2f4f3a;
  background: #18241c;
  color: #b7e0c3;
  border-radius: var(--radius);
  margin-bottom: var(--space-4);
}
</style>
