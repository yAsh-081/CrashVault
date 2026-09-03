<script setup lang="ts">
import { signalClass, signalName } from "../utils";

defineProps<{
  signal: number;
}>();
</script>

<template>
  <span class="signal-badge" :class="signalClass(signal)">
    {{ signalName(signal) }}
  </span>
</template>
