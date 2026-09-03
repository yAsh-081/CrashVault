<script setup lang="ts">
import { onMounted, ref, watch } from "vue";

const props = withDefaults(
  defineProps<{
    modelValue: string;
    placeholder?: string;
    mono?: boolean;
  }>(),
  {
    placeholder: "",
    mono: false,
  },
);

const emit = defineEmits<{
  "update:modelValue": [string];
}>();

const editor = ref<HTMLElement | null>(null);

function syncFromDom() {
  const text = editor.value?.innerText.replace(/\n/g, "") ?? "";
  if (text !== props.modelValue) {
    emit("update:modelValue", text);
  }
}

function onPaste(event: ClipboardEvent) {
  event.preventDefault();
  const text = event.clipboardData?.getData("text/plain") ?? "";
  document.execCommand("insertText", false, text);
}

watch(
  () => props.modelValue,
  (value) => {
    if (editor.value && editor.value.innerText !== value) {
      editor.value.innerText = value;
    }
  },
);

onMounted(() => {
  if (editor.value && props.modelValue) {
    editor.value.innerText = props.modelValue;
  }
});
</script>

<template>
  <div class="text-field" :class="{ mono }">
    <div
      ref="editor"
      class="plain-text-input"
      contenteditable="true"
      spellcheck="false"
      :data-placeholder="placeholder"
      @input="syncFromDom"
      @paste="onPaste"
    />
  </div>
</template>

<style scoped>
.plain-text-input {
  display: block;
  width: 100%;
  min-width: 0;
  margin: 0;
  padding: 0;
  border: 0;
  outline: none;
  background-color: #1e2229;
  color: #e8eaed;
  font: inherit;
  line-height: 1.45;
  white-space: nowrap;
  overflow: hidden;
  cursor: text;
  user-select: text;
}

.text-field:hover .plain-text-input,
.text-field:focus-within .plain-text-input {
  background-color: #252a33;
}

.plain-text-input:empty::before {
  content: attr(data-placeholder);
  color: #6b7280;
  pointer-events: none;
}
</style>
