<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import { useRoute, useRouter } from "vue-router";
import { getCrashGroupDetail, getOccurrenceDetail, type CrashGroupDetail, type OccurrenceDetail } from "../backend";
import SignalBadge from "../components/SignalBadge.vue";
import { displayFunction, formatHex, formatTimestamp, truncate } from "../utils";

const props = defineProps<{ id: string }>();
const route = useRoute();
const router = useRouter();

const loading = ref(true);
const error = ref<string | null>(null);
const group = ref<CrashGroupDetail | null>(null);
const occurrence = ref<OccurrenceDetail | null>(null);
const selectedOccurrenceId = ref<number | null>(null);

const groupId = computed(() => Number(props.id));

async function loadGroup() {
  loading.value = true;
  error.value = null;
  try {
    group.value = await getCrashGroupDetail(groupId.value);
    const queryOcc = route.query.occurrence;
    const initialId =
      typeof queryOcc === "string"
        ? Number(queryOcc)
        : group.value.occurrences[0]?.id ?? null;
    selectedOccurrenceId.value = initialId;
    if (initialId) {
      occurrence.value = await getOccurrenceDetail(initialId);
    }
  } catch (err) {
    error.value = err instanceof Error ? err.message : "Failed to load crash group";
    group.value = null;
    occurrence.value = null;
  } finally {
    loading.value = false;
  }
}

async function selectOccurrence(id: number) {
  selectedOccurrenceId.value = id;
  occurrence.value = await getOccurrenceDetail(id);
}

onMounted(loadGroup);
watch(() => props.id, loadGroup);

const registerEntries = computed(() => {
  if (!occurrence.value) {
    return [];
  }
  const r = occurrence.value.registers;
  return [
    ["RAX", r.rax],
    ["RBX", r.rbx],
    ["RCX", r.rcx],
    ["RDX", r.rdx],
    ["RSI", r.rsi],
    ["RDI", r.rdi],
    ["RBP", r.rbp],
    ["RSP", r.rsp],
    ["R8", r.r8],
    ["R9", r.r9],
    ["R10", r.r10],
    ["R11", r.r11],
    ["R12", r.r12],
    ["R13", r.r13],
    ["R14", r.r14],
    ["R15", r.r15],
    ["RIP", r.rip],
  ] as const;
});
</script>

<template>
  <div class="page">
    <span class="btn btn-ghost back" @click="router.push('/crashes')">
      <span class="back-arrow" aria-hidden="true"></span>
      Back to crashes
    </span>

    <div v-if="error" class="error-banner">{{ error }}</div>
    <div v-if="loading" class="loading">Loading crash detail…</div>

    <template v-else-if="group">
      <header class="detail-header">
        <div>
          <SignalBadge :signal="group.signal" />
          <h1 class="title mono">{{ displayFunction(group.top_function) }}</h1>
          <p class="subtitle">
            {{ group.application }} · {{ group.occurrence_count }} occurrence{{
              group.occurrence_count === 1 ? "" : "s"
            }}
          </p>
          <p class="meta">
            First seen {{ formatTimestamp(group.first_seen) }} · Last seen
            {{ formatTimestamp(group.last_seen) }}
          </p>
        </div>
      </header>

      <div class="layout">
        <div class="main-col">
          <section class="panel">
            <div class="panel-header">Fault</div>
            <div class="panel-body fault-grid">
              <div><span class="label">Signal</span><div>{{ group.signal_name }}</div></div>
              <div>
                <span class="label">Fault address</span>
                <div class="mono">{{ formatHex(occurrence?.fault_addr ?? null) }}</div>
              </div>
              <div>
                <span class="label">PID / TID</span>
                <div class="mono">
                  {{ occurrence?.pid ?? "—" }} / {{ occurrence?.tid ?? "—" }}
                </div>
              </div>
              <div>
                <span class="label">Executable</span>
                <div class="mono" :title="occurrence?.executable_path ?? undefined">
                  {{ truncate(occurrence?.executable_path ?? "—", 64) }}
                </div>
              </div>
            </div>
          </section>

          <section class="panel">
            <div class="panel-header">
              Stack Trace
              <span class="hint">{{ occurrence?.frames.length ?? 0 }} frame captured</span>
            </div>
            <div class="panel-body">
              <div v-for="frame in occurrence?.frames ?? []" :key="frame.frame_index" class="stack-frame">
                <div class="stack-index">#{{ frame.frame_index }}</div>
                <div class="stack-fn mono">{{ displayFunction(frame.function_name) }}</div>
                <div v-if="frame.source_file" class="stack-loc">
                  {{ frame.source_file }}:{{ frame.source_line ?? "?" }}
                </div>
                <div class="stack-addr">
                  {{ formatHex(frame.raw_address) }}
                  <template v-if="frame.normalized_address !== null">
                    · offset {{ formatHex(frame.normalized_address) }}
                  </template>
                </div>
              </div>
              <div v-if="!occurrence?.frames.length" class="empty-state">
                <p>No symbolized frames for this occurrence.</p>
              </div>
            </div>
          </section>

          <section class="panel">
            <div class="panel-header">Registers</div>
            <div class="panel-body register-grid">
              <div
                v-for="[name, value] in registerEntries"
                :key="name"
                class="register-item"
                :class="{ highlight: name === 'RIP' || name === 'RSP' || name === 'RBP' }"
              >
                <div class="register-label">{{ name }}</div>
                <div class="register-value">{{ formatHex(value) }}</div>
              </div>
            </div>
          </section>
        </div>

        <aside class="side-col">
          <section class="panel">
            <div class="panel-header">Occurrence History</div>
            <div class="panel-body occurrence-list">
              <div
                v-for="item in group.occurrences"
                :key="item.id"
                class="occurrence-item"
                :class="{ selected: item.id === selectedOccurrenceId }"
                @click="selectOccurrence(item.id)"
              >
                <div>{{ formatTimestamp(item.imported_at) }}</div>
                <div class="occ-meta mono">
                  v{{ item.app_version ?? "?" }} · pid {{ item.pid ?? "?" }} · tid
                  {{ item.tid ?? "?" }}
                </div>
              </div>
            </div>
          </section>

          <section class="panel">
            <div class="panel-header">Versions</div>
            <div class="panel-body">
              <div v-for="v in group.versions" :key="v.version" class="version-row">
                <span>{{ v.version }}</span>
                <span class="mono">{{ v.count }}</span>
              </div>
              <div v-if="!group.versions.length" class="empty-state"><p>—</p></div>
            </div>
          </section>
        </aside>
      </div>
    </template>
  </div>
</template>

<style scoped>
.back {
  margin-bottom: var(--space-3);
}

.back-arrow {
  display: inline-block;
  width: 7px;
  height: 7px;
  border-left: 1.5px solid currentColor;
  border-bottom: 1.5px solid currentColor;
  transform: rotate(45deg);
  margin-right: 2px;
}

.detail-header {
  margin-bottom: var(--space-4);
}

.title {
  margin: var(--space-2) 0 0;
  font-size: 22px;
}

.subtitle,
.meta {
  margin: var(--space-2) 0 0;
  color: var(--text-secondary);
}

.layout {
  display: grid;
  grid-template-columns: 1fr 300px;
  gap: var(--space-4);
}

.fault-grid {
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

.hint {
  float: right;
  text-transform: none;
  letter-spacing: normal;
  font-weight: 400;
  color: var(--text-muted);
}

.occurrence-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
  max-height: 360px;
  overflow: auto;
}

.occurrence-item {
  text-align: left;
  padding: 8px 10px;
  border-radius: var(--radius);
  background-color: #1e2229;
  border: 1px solid #232830;
  color: #e8eaed;
  cursor: pointer;
  user-select: none;
}

.occurrence-item:hover {
  background-color: #252a33;
  border-color: #2d333d;
}

.occurrence-item.selected {
  background-color: #1f2a3d;
  border-color: #3a6fc7;
}

.occ-meta {
  margin-top: 4px;
  color: var(--text-muted);
  font-size: 11px;
}

.version-row {
  display: flex;
  justify-content: space-between;
  padding: 6px 0;
  border-bottom: 1px solid var(--border-subtle);
}

@media (max-width: 1100px) {
  .layout {
    grid-template-columns: 1fr;
  }
}
</style>
