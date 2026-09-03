<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import { RouterLink, RouterView, useRoute } from "vue-router";
import { getWatcherStatus, onImportComplete, type WatcherStatus } from "../backend";
import { formatRelative } from "../utils";

const route = useRoute();
const status = ref<WatcherStatus | null>(null);
const refreshKey = ref(0);
let unlisten: (() => void) | null = null;
let pollTimer: number | null = null;

const lastImportLabel = computed(() => {
  if (!status.value?.last_import_at) {
    return "No imports yet";
  }
  return `Last import: ${formatRelative(status.value.last_import_at)}`;
});

async function loadStatus() {
  try {
    status.value = await getWatcherStatus();
  } catch {
    status.value = null;
  }
}

function bumpRefresh() {
  refreshKey.value += 1;
}

onMounted(async () => {
  await loadStatus();
  unlisten = await onImportComplete(async () => {
    await loadStatus();
    bumpRefresh();
  });
  pollTimer = window.setInterval(loadStatus, 10000);
});

onUnmounted(() => {
  if (unlisten) {
    unlisten();
  }
  if (pollTimer !== null) {
    window.clearInterval(pollTimer);
  }
});

const nav = [
  { to: "/dashboard", label: "Dashboard" },
  { to: "/crashes", label: "Crashes" },
  { to: "/applications", label: "Applications" },
  { to: "/settings", label: "Settings" },
];
</script>

<template>
  <div class="shell">
    <header class="topbar panel">
      <div class="brand">CrashVault</div>
      <div class="status">
        <span
          class="status-dot"
          :class="status?.watching ? 'watching' : 'idle'"
          aria-hidden="true"
        />
        <span>{{ status?.watching ? "Watching" : "Idle" }}</span>
        <span class="status-sep">·</span>
        <span class="status-meta">{{ lastImportLabel }}</span>
        <span v-if="status?.last_error" class="status-error" :title="status.last_error">
          Import issue
        </span>
      </div>
    </header>

    <div class="body">
      <aside class="sidebar panel">
        <nav>
          <RouterLink
            v-for="item in nav"
            :key="item.to"
            :to="item.to"
            class="nav-link"
            :class="{ active: route.path.startsWith(item.to) }"
          >
            {{ item.label }}
          </RouterLink>
        </nav>
      </aside>

      <main class="content">
        <RouterView :key="refreshKey" />
      </main>
    </div>
  </div>
</template>

<style scoped>
.shell {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 16px;
  border-radius: 0;
  border-left: none;
  border-right: none;
  border-top: none;
}

.brand {
  font-weight: 700;
  letter-spacing: 0.02em;
}

.status {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--text-secondary);
  font-size: 12px;
}

.status-sep {
  color: var(--text-muted);
}

.status-meta {
  color: var(--text-muted);
}

.status-error {
  color: var(--danger);
  font-size: 11px;
}

.body {
  display: flex;
  flex: 1;
  min-height: 0;
}

.sidebar {
  width: var(--sidebar-width);
  flex-shrink: 0;
  border-radius: 0;
  border-top: none;
  border-bottom: none;
  border-left: none;
}

nav {
  display: flex;
  flex-direction: column;
  padding: 8px;
  gap: 4px;
}

.nav-link {
  padding: 8px 10px;
  border-radius: var(--radius);
  color: var(--text-secondary);
}

.nav-link:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.nav-link.active {
  background: #1f2a3d;
  color: var(--text-primary);
  border: 1px solid #2f3f58;
}

.content {
  flex: 1;
  min-width: 0;
  padding: var(--space-4);
  overflow: auto;
}
</style>
