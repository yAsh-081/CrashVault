import { createRouter, createWebHistory } from "vue-router";
import AppShell from "./components/AppShell.vue";
import DashboardView from "./views/DashboardView.vue";
import CrashesView from "./views/CrashesView.vue";
import CrashDetailView from "./views/CrashDetailView.vue";
import ApplicationsView from "./views/ApplicationsView.vue";
import SettingsView from "./views/SettingsView.vue";

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: "/",
      component: AppShell,
      children: [
        { path: "", redirect: "/dashboard" },
        { path: "dashboard", name: "dashboard", component: DashboardView },
        { path: "crashes", name: "crashes", component: CrashesView },
        { path: "crashes/:id", name: "crash-detail", component: CrashDetailView, props: true },
        { path: "applications", name: "applications", component: ApplicationsView },
        { path: "settings", name: "settings", component: SettingsView },
      ],
    },
  ],
});

export default router;
