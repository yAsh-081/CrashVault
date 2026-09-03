<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from "vue";

export interface FilterOption {
  value: string;
  label: string;
}

const props = defineProps<{
  modelValue: string;
  options: FilterOption[];
}>();

const emit = defineEmits<{
  "update:modelValue": [string];
}>();

const open = ref(false);
const root = ref<HTMLElement | null>(null);
const trigger = ref<HTMLElement | null>(null);
const menuStyle = ref<Record<string, string>>({});
const triggerHover = ref(false);
const hoveredOption = ref<string | null>(null);

const currentLabel = computed(
  () => props.options.find((o) => o.value === props.modelValue)?.label ?? "",
);

function positionMenu() {
  const el = trigger.value;
  if (!el) {
    return;
  }
  const rect = el.getBoundingClientRect();
  menuStyle.value = {
    position: "fixed",
    top: `${rect.bottom + 4}px`,
    left: `${rect.left}px`,
    width: `${rect.width}px`,
    zIndex: "9999",
  };
}

async function toggle() {
  open.value = !open.value;
  if (open.value) {
    await nextTick();
    positionMenu();
  }
}

function pick(value: string) {
  emit("update:modelValue", value);
  open.value = false;
}

function onDocumentClick(event: MouseEvent) {
  if (root.value && !root.value.contains(event.target as Node)) {
    const menu = document.getElementById(menuId);
    if (menu && menu.contains(event.target as Node)) {
      return;
    }
    open.value = false;
  }
}

function onWindowChange() {
  if (open.value) {
    positionMenu();
  }
}

const menuId = `filter-menu-${Math.random().toString(36).slice(2, 9)}`;

watch(open, (isOpen) => {
  if (isOpen) {
    nextTick(positionMenu);
  }
});

onMounted(() => {
  document.addEventListener("click", onDocumentClick);
  window.addEventListener("resize", onWindowChange);
  window.addEventListener("scroll", onWindowChange, true);
});

onUnmounted(() => {
  document.removeEventListener("click", onDocumentClick);
  window.removeEventListener("resize", onWindowChange);
  window.removeEventListener("scroll", onWindowChange, true);
});
</script>

<template>
  <div ref="root" class="filter-select" :class="{ open }">
    <div
      ref="trigger"
      class="filter-select-trigger"
      :class="{ hovered: triggerHover || open }"
      @mouseenter="triggerHover = true"
      @mouseleave="triggerHover = false"
      @click.stop="toggle"
    >
      <span class="filter-select-label">{{ currentLabel }}</span>
      <span class="filter-select-chevron" aria-hidden="true">▾</span>
    </div>
    <Teleport to="body">
      <div
        v-if="open"
        :id="menuId"
        class="filter-select-menu"
        :style="menuStyle"
        @click.stop
        @mousedown.prevent
      >
        <div
          v-for="opt in options"
          :key="opt.value"
          class="filter-select-option"
          :class="{ active: opt.value === modelValue, hovered: hoveredOption === opt.value }"
          @mouseenter="hoveredOption = opt.value"
          @mouseleave="hoveredOption = null"
          @click="pick(opt.value)"
        >
          {{ opt.label }}
        </div>
      </div>
    </Teleport>
  </div>
</template>
